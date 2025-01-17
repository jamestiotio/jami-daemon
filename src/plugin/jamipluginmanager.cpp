/*
 *  Copyright (C) 2020-2023 Savoir-faire Linux Inc.
 *
 *  Author: Aline Gondim Santos <aline.gondimsantos@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */

#include "jamipluginmanager.h"
#include "pluginsutils.h"
#include "fileutils.h"
#include "archiver.h"

#include "logger.h"

#include <fstream>
#include <stdexcept>
#include <msgpack.hpp>
#include "manager.h"
#include "preferences.h"
#include "jami/plugin_manager_interface.h"

#define PLUGIN_ALREADY_INSTALLED 100 /* Plugin already installed with the same version */
#define PLUGIN_OLD_VERSION       200 /* Plugin already installed with a newer version */

#ifdef WIN32
#define LIB_TYPE   ".dll"
#define LIB_PREFIX ""
#else
#ifdef __APPLE__
#define LIB_TYPE   ".dylib"
#define LIB_PREFIX "lib"
#else
#define LIB_TYPE   ".so"
#define LIB_PREFIX "lib"
#endif
#endif

namespace jami {

std::map<std::string, std::string>
JamiPluginManager::getPluginDetails(const std::string& rootPath)
{
    auto detailsIt = pluginDetailsMap_.find(rootPath);
    if (detailsIt != pluginDetailsMap_.end()) {
        return detailsIt->second;
    }

    std::map<std::string, std::string> details = PluginUtils::parseManifestFile(
        PluginUtils::manifestPath(rootPath));
    if (!details.empty()) {
        auto it = details.find("iconPath");
        it->second.insert(0, rootPath + DIR_SEPARATOR_CH + "data" + DIR_SEPARATOR_CH);
        details["soPath"] = rootPath + DIR_SEPARATOR_CH + LIB_PREFIX + details["name"] + LIB_TYPE;
        detailsIt = pluginDetailsMap_.emplace(rootPath, std::move(details)).first;
        return detailsIt->second;
    }
    return {};
}

std::vector<std::string>
JamiPluginManager::getInstalledPlugins()
{
    // Gets all plugins in standard path
    std::string pluginsPath = fileutils::get_data_dir() + DIR_SEPARATOR_CH + "plugins";
    std::vector<std::string> pluginsPaths = fileutils::readDirectory(pluginsPath);
    std::for_each(pluginsPaths.begin(), pluginsPaths.end(), [&pluginsPath](std::string& x) {
        x = pluginsPath + DIR_SEPARATOR_CH + x;
    });
    auto returnIterator = std::remove_if(pluginsPaths.begin(),
                                         pluginsPaths.end(),
                                         [](const std::string& path) {
                                             return !PluginUtils::checkPluginValidity(path);
                                         });
    pluginsPaths.erase(returnIterator, std::end(pluginsPaths));

    // Gets plugins installed in non standard path
    std::vector<std::string> nonStandardInstalls = jami::Manager::instance()
                                                       .pluginPreferences.getInstalledPlugins();
    for (auto& path : nonStandardInstalls) {
        if (PluginUtils::checkPluginValidity(path))
            pluginsPaths.emplace_back(path);
    }

    return pluginsPaths;
}

int
JamiPluginManager::installPlugin(const std::string& jplPath, bool force)
{
    int r {0};
    if (fileutils::isFile(jplPath)) {
        try {
            auto manifestMap = PluginUtils::readPluginManifestFromArchive(jplPath);
            const std::string& name = manifestMap["name"];
            if (name.empty())
                return 0;
            const std::string& version = manifestMap["version"];
            std::string destinationDir {fileutils::get_data_dir() + DIR_SEPARATOR_CH + "plugins"
                                        + DIR_SEPARATOR_CH + name};
            // Find if there is an existing version of this plugin
            const auto alreadyInstalledManifestMap = PluginUtils::parseManifestFile(
                PluginUtils::manifestPath(destinationDir));

            if (!alreadyInstalledManifestMap.empty()) {
                if (force) {
                    r = uninstallPlugin(destinationDir);
                    if (r == 0) {
                        archiver::uncompressArchive(jplPath,
                                                    destinationDir,
                                                    PluginUtils::uncompressJplFunction);
                    }
                } else {
                    std::string installedVersion = alreadyInstalledManifestMap.at("version");
                    if (version > installedVersion) {
                        r = uninstallPlugin(destinationDir);
                        if (r == 0) {
                            archiver::uncompressArchive(jplPath,
                                                        destinationDir,
                                                        PluginUtils::uncompressJplFunction);
                        }
                    } else if (version == installedVersion) {
                        r = PLUGIN_ALREADY_INSTALLED;
                    } else {
                        r = PLUGIN_OLD_VERSION;
                    }
                }
            } else {
                archiver::uncompressArchive(jplPath,
                                            destinationDir,
                                            PluginUtils::uncompressJplFunction);
            }
            libjami::loadPlugin(destinationDir);
        } catch (const std::exception& e) {
            JAMI_ERR() << e.what();
        }
    }
    return r;
}

int
JamiPluginManager::uninstallPlugin(const std::string& rootPath)
{
    if (PluginUtils::checkPluginValidity(rootPath)) {
        auto detailsIt = pluginDetailsMap_.find(rootPath);
        if (detailsIt != pluginDetailsMap_.end()) {
            bool loaded = pm_.checkLoadedPlugin(rootPath);
            if (loaded) {
                JAMI_INFO() << "PLUGIN: unloading before uninstall.";
                bool status = libjami::unloadPlugin(rootPath);
                if (!status) {
                    JAMI_INFO() << "PLUGIN: could not unload, not performing uninstall.";
                    return -1;
                }
            }
            for (const auto& accId : jami::Manager::instance().getAccountList())
                fileutils::removeAll(fileutils::get_data_dir() + DIR_SEPARATOR_CH + accId
                                     + DIR_SEPARATOR_CH + "plugins" + DIR_SEPARATOR_CH
                                     + detailsIt->second.at("name"));
            pluginDetailsMap_.erase(detailsIt);
        }
        return fileutils::removeAll(rootPath);
    } else {
        JAMI_INFO() << "PLUGIN: not installed.";
        return -1;
    }
}

bool
JamiPluginManager::loadPlugin(const std::string& rootPath)
{
#ifdef ENABLE_PLUGIN
    try {
        bool status = pm_.load(getPluginDetails(rootPath).at("soPath"));
        JAMI_INFO() << "PLUGIN: load status - " << status;

        return status;

    } catch (const std::exception& e) {
        JAMI_ERR() << e.what();
        return false;
    }
#endif
    return false;
}

bool
JamiPluginManager::unloadPlugin(const std::string& rootPath)
{
#ifdef ENABLE_PLUGIN
    try {
        bool status = pm_.unload(getPluginDetails(rootPath).at("soPath"));
        JAMI_INFO() << "PLUGIN: unload status - " << status;

        return status;
    } catch (const std::exception& e) {
        JAMI_ERR() << e.what();
        return false;
    }
#endif
    return false;
}

std::vector<std::string>
JamiPluginManager::getLoadedPlugins() const
{
    std::vector<std::string> loadedSoPlugins = pm_.getLoadedPlugins();
    std::vector<std::string> loadedPlugins {};
    loadedPlugins.reserve(loadedSoPlugins.size());
    std::transform(loadedSoPlugins.begin(),
                   loadedSoPlugins.end(),
                   std::back_inserter(loadedPlugins),
                   [](const std::string& soPath) {
                       return PluginUtils::getRootPathFromSoPath(soPath);
                   });
    return loadedPlugins;
}

std::vector<std::map<std::string, std::string>>
JamiPluginManager::getPluginPreferences(const std::string& rootPath, const std::string& accountId)
{
    return PluginPreferencesUtils::getPreferences(rootPath, accountId);
}

bool
JamiPluginManager::setPluginPreference(const std::string& rootPath,
                                       const std::string& accountId,
                                       const std::string& key,
                                       const std::string& value)
{
    std::string acc = accountId;

    // If we try to change a preference value linked to an account
    // but that preference is global, we must ignore accountId and
    // change the preference for every account
    if (!accountId.empty()) {
        // Get global preferences
        auto preferences = PluginPreferencesUtils::getPreferences(rootPath, "");
        // Check if the preference we want to change is global
        auto it = std::find_if(preferences.cbegin(),
                               preferences.cend(),
                               [key](const std::map<std::string, std::string>& preference) {
                                   return preference.at("key") == key;
                               });
        // Ignore accountId if global preference
        if (it != preferences.cend())
            acc.clear();
    }

    std::map<std::string, std::string> pluginUserPreferencesMap
        = PluginPreferencesUtils::getUserPreferencesValuesMap(rootPath, acc);
    std::map<std::string, std::string> pluginPreferencesMap
        = PluginPreferencesUtils::getPreferencesValuesMap(rootPath, acc);

    // If any plugin handler is active we may have to reload it
    bool force {pm_.checkLoadedPlugin(rootPath)};

    // We check if the preference is modified without having to reload plugin
    force &= preferencesm_.setPreference(key, value, rootPath, acc);
    force &= callsm_.setPreference(key, value, rootPath);
    force &= chatsm_.setPreference(key, value, rootPath);

    if (force)
        unloadPlugin(rootPath);

    // Save preferences.msgpack with modified preferences values
    auto find = pluginPreferencesMap.find(key);
    if (find != pluginPreferencesMap.end()) {
        pluginUserPreferencesMap[key] = value;
        const std::string preferencesValuesFilePath
            = PluginPreferencesUtils::valuesFilePath(rootPath, acc);
        std::lock_guard<std::mutex> guard(fileutils::getFileLock(preferencesValuesFilePath));
        std::ofstream fs(preferencesValuesFilePath, std::ios::binary);
        if (!fs.good()) {
            if (force) {
                loadPlugin(rootPath);
            }
            return false;
        }
        try {
            msgpack::pack(fs, pluginUserPreferencesMap);
        } catch (const std::exception& e) {
            JAMI_ERR() << e.what();
            if (force) {
                loadPlugin(rootPath);
            }
            return false;
        }
    }
    if (force) {
        loadPlugin(rootPath);
    }
    return true;
}

std::map<std::string, std::string>
JamiPluginManager::getPluginPreferencesValuesMap(const std::string& rootPath,
                                                 const std::string& accountId)
{
    return PluginPreferencesUtils::getPreferencesValuesMap(rootPath, accountId);
}

bool
JamiPluginManager::resetPluginPreferencesValuesMap(const std::string& rootPath,
                                                   const std::string& accountId)
{
    bool acc {accountId.empty()};
    bool loaded {pm_.checkLoadedPlugin(rootPath)};
    if (loaded && acc)
        unloadPlugin(rootPath);
    auto status = PluginPreferencesUtils::resetPreferencesValuesMap(rootPath, accountId);
    preferencesm_.resetPreferences(rootPath, accountId);
    if (loaded && acc) {
        loadPlugin(rootPath);
    }
    return status;
}

void
JamiPluginManager::registerServices()
{
    // Register getPluginPreferences so that plugin's can receive it's preferences
    pm_.registerService("getPluginPreferences", [](const DLPlugin* plugin, void* data) {
        auto ppp = static_cast<std::map<std::string, std::string>*>(data);
        *ppp = PluginPreferencesUtils::getPreferencesValuesMap(
            PluginUtils::getRootPathFromSoPath(plugin->getPath()));
        return 0;
    });

    // Register getPluginDataPath so that plugin's can receive the path to it's data folder
    pm_.registerService("getPluginDataPath", [](const DLPlugin* plugin, void* data) {
        auto dataPath = static_cast<std::string*>(data);
        dataPath->assign(PluginUtils::dataPath(plugin->getPath()));
        return 0;
    });

    // getPluginAccPreferences is a service that allows plugins to load saved per account preferences.
    auto getPluginAccPreferences = [](const DLPlugin* plugin, void* data) {
        const auto path = PluginUtils::getRootPathFromSoPath(plugin->getPath());
        auto preferencesPtr {(static_cast<PreferencesMap*>(data))};
        if (!preferencesPtr)
            return -1;

        preferencesPtr->emplace("default",
                                PluginPreferencesUtils::getPreferencesValuesMap(path, "default"));

        for (const auto& accId : jami::Manager::instance().getAccountList())
            preferencesPtr->emplace(accId,
                                    PluginPreferencesUtils::getPreferencesValuesMap(path, accId));
        return 0;
    };

    pm_.registerService("getPluginAccPreferences", getPluginAccPreferences);
}
} // namespace jami
