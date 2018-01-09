#include <cstring>
#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <fcntl.h>
#include "symbols/android_symbols.h"
#include "symbols/egl_symbols.h"
#include "symbols/gles_symbols.h"
#include "symbols/fmod_symbols.h"
#include "symbols/libm_symbols.h"
#include "common.h"
#include "log.h"
#include "linux_appplatform.h"
#include "minecraft/Api.h"
#include "minecraft/Whitelist.h"
#include "minecraft/OpsList.h"
#include "minecraft/ResourcePack.h"
#include "minecraft/FilePathManager.h"
#include "minecraft/MinecraftEventing.h"
#include "minecraft/UUID.h"
#include "minecraft/LevelSettings.h"
#include "minecraft/ServerInstance.h"
#include "minecraft/UserManager.h"
#include "minecraft/AutomationClient.h"
#include "minecraft/Scheduler.h"
#include "minecraft/Minecraft.h"
#include "minecraft/MinecraftCommands.h"
#include "minecraft/DedicatedServerCommandOrigin.h"
#include "minecraft/CommandOutputSender.h"
#include "minecraft/CommandOutput.h"
#include "minecraft/I18n.h"
#include "minecraft/ResourcePackStack.h"
#include "server_minecraft_app.h"
#include "server_properties.h"

extern "C" {
#include "../hybris/include/hybris/dlfcn.h"
#include "../hybris/include/hybris/hook.h"
#include "../hybris/src/jb/linker.h"
}

void stubFunc() {
    Log::warn("Launcher", "Stubbed function call");
}

int main(int argc, char *argv[]) {
    registerCrashHandler();

    // We're going to look at the CWD instead of the proper assets folders because to support other paths I'd likely
    // have to register a proper asset loader in MCPE, and the default one just falls back to the current directory for
    // assets, so let's at least use the CWD for as much stuff as possible.
    std::string cwd = PathHelper::getWorkingDir();

    ServerProperties properties;
    {
        std::ifstream propertiesFile("server.properties");
        if (propertiesFile)
            properties.load(propertiesFile);
    }

    Log::trace("Launcher", "Loading hybris libraries");
    stubSymbols(android_symbols, (void*) stubFunc);
    stubSymbols(egl_symbols, (void*) stubFunc);
    stubSymbols(gles_symbols, (void*) stubFunc);
    stubSymbols(fmod_symbols, (void*) stubFunc);
    hybris_hook("eglGetProcAddress", (void*) stubFunc);
    void* libmLib = loadLibraryOS("libm.so.6", libm_symbols);
    hookAndroidLog();
    if (!load_empty_library("libc.so") || !load_empty_library("libm.so"))
        return -1;
    // load stub libraries
    if (!load_empty_library("libandroid.so") || !load_empty_library("liblog.so") || !load_empty_library("libEGL.so") || !load_empty_library("libGLESv2.so") || !load_empty_library("libOpenSLES.so") || !load_empty_library("libfmod.so") || !load_empty_library("libGLESv1_CM.so"))
        return -1;
    if (!load_empty_library("libmcpelauncher_mod.so"))
        return -1;
    Log::trace("Launcher", "Loading Minecraft library");
    void* handle = hybris_dlopen((cwd + "libs/libminecraftpe.so").c_str(), RTLD_LAZY);
    if (handle == nullptr) {
        Log::error("Launcher", "Failed to load Minecraft: %s", hybris_dlerror());
        return -1;
    }

    unsigned int libBase = ((soinfo*) handle)->base;
    Log::info("Launcher", "Loaded Minecraft library");
    Log::debug("Launcher", "Minecraft is at offset 0x%x", libBase);

    mcpe::string::empty = (mcpe::string*) hybris_dlsym(handle, "_ZN4Util12EMPTY_STRINGE");

    AppPlatform::myVtable = (void**) hybris_dlsym(handle, "_ZTV11AppPlatform");
    AppPlatform::_singleton = (AppPlatform**) hybris_dlsym(handle, "_ZN11AppPlatform10mSingletonE");
    AppPlatform::AppPlatform_construct = (void (*)(AppPlatform*)) hybris_dlsym(handle, "_ZN11AppPlatformC2Ev");
    AppPlatform::AppPlatform_initialize = (void (*)(AppPlatform*)) hybris_dlsym(handle, "_ZN11AppPlatform10initializeEv");
    AppPlatform::AppPlatform__fireAppFocusGained = (void (*)(AppPlatform*)) hybris_dlsym(handle, "_ZN11AppPlatform19_fireAppFocusGainedEv");

    LevelSettings::LevelSettings_construct = (void (*)(LevelSettings*)) hybris_dlsym(handle, "_ZN13LevelSettingsC2Ev");
    LevelSettings::LevelSettings_construct2 = (void (*)(LevelSettings*, LevelSettings const&)) hybris_dlsym(handle, "_ZN13LevelSettingsC2ERKS_");
    MinecraftEventing::MinecraftEventing_construct = (void (*)(MinecraftEventing*, mcpe::string const&)) hybris_dlsym(handle, "_ZN17MinecraftEventingC2ERKSs");
    MinecraftEventing::MinecraftEventing_init = (void (*)(MinecraftEventing*)) hybris_dlsym(handle, "_ZN17MinecraftEventing4initEv");
    SkinPackKeyProvider::SkinPackKeyProvider_construct = (void (*)(SkinPackKeyProvider*)) hybris_dlsym(handle, "_ZN19SkinPackKeyProviderC2Ev");
    PackManifestFactory::PackManifestFactory_construct = (void (*)(PackManifestFactory*, IPackTelemetry&)) hybris_dlsym(handle, "_ZN19PackManifestFactoryC2ER14IPackTelemetry");
    PackSourceFactory::PackSourceFactory_construct = (void (*)(PackSourceFactory*, Options*)) hybris_dlsym(handle, "_ZN17PackSourceFactoryC2EP7Options");
    ResourcePackManager::ResourcePackManager_construct = (void (*)(ResourcePackManager*, std::function<mcpe::string ()> const&, ContentTierManager const&)) hybris_dlsym(handle, "_ZN19ResourcePackManagerC2ESt8functionIFSsvEERK18ContentTierManager");
    ResourcePackManager::ResourcePackManager_setStack = (void (*)(ResourcePackManager*, std::unique_ptr<ResourcePackStack>, ResourcePackStackType, bool)) hybris_dlsym(handle, "_ZN19ResourcePackManager8setStackESt10unique_ptrI17ResourcePackStackSt14default_deleteIS1_EE21ResourcePackStackTypeb");
    ResourcePackManager::ResourcePackManager_onLanguageChanged = (void (*)(ResourcePackManager*)) hybris_dlsym(handle, "_ZN19ResourcePackManager17onLanguageChangedEv");
    ResourcePackRepository::ResourcePackRepository_construct = (void (*)(ResourcePackRepository*, MinecraftEventing&, PackManifestFactory&, IContentAccessibilityProvider&, FilePathManager*, PackSourceFactory &)) hybris_dlsym(handle, "_ZN22ResourcePackRepositoryC2ER17MinecraftEventingR19PackManifestFactoryR29IContentAccessibilityProviderP15FilePathManagerR17PackSourceFactory");
    ContentTierManager::ContentTierManager_construct = (void (*)(ContentTierManager*)) hybris_dlsym(handle, "_ZN18ContentTierManagerC2Ev");
    FilePathManager::FilePathManager_construct = (void (*)(FilePathManager*, mcpe::string, bool)) hybris_dlsym(handle, "_ZN15FilePathManagerC2ESsb");
    ((void*&) ServerInstance::ServerInstance_construct) = hybris_dlsym(handle, "_ZN14ServerInstanceC2ER13IMinecraftAppRK9WhitelistRK7OpsListP15FilePathManagerNSt6chrono8durationIxSt5ratioILx1ELx1EEEESsSsSsRK19IContentKeyProviderSs13LevelSettingsRN9minecraft3api3ApiEibiiibRKSt6vectorISsSaISsEESsRKN3mce4UUIDER17MinecraftEventingR14NetworkHandlerR22ResourcePackRepositoryRK18ContentTierManagerR19ResourcePackManagerPS15_St8functionIFvRKSsEE");
    ServerInstance::ServerInstance_update = (void (*)(ServerInstance*)) hybris_dlsym(handle, "_ZN14ServerInstance6updateEv");
    ServerInstance::ServerInstance_mainThreadNetworkUpdate_HACK = (void (*)(ServerInstance*)) hybris_dlsym(handle, "_ZN14ServerInstance28mainThreadNetworkUpdate_HACKEv");
    mce::UUID::EMPTY = (mce::UUID*) hybris_dlsym(handle, "_ZN3mce4UUID5EMPTYE");
    mce::UUID::fromString = (mce::UUID (*)(mcpe::string const&)) hybris_dlsym(handle, "_ZN3mce4UUID10fromStringERKSs");
    NetworkHandler::NetworkHandler_construct = (void (*)(NetworkHandler*)) hybris_dlsym(handle, "_ZN14NetworkHandlerC2Ev");
    Social::UserManager::CreateUserManager = (std::unique_ptr<Social::UserManager> (*)()) hybris_dlsym(handle, "_ZN6Social11UserManager17CreateUserManagerEv");
    Automation::AutomationClient::AutomationClient_construct = (void (*)(Automation::AutomationClient*, IMinecraftApp&)) hybris_dlsym(handle, "_ZN10Automation16AutomationClientC2ER13IMinecraftApp");
    Scheduler::singleton = (Scheduler* (*)()) hybris_dlsym(handle, "_ZN9Scheduler9singletonEv");
    Scheduler::Scheduler_processCoroutines = (void (*)(Scheduler*, std::chrono::duration<long long>)) hybris_dlsym(handle, "_ZN9Scheduler17processCoroutinesENSt6chrono8durationIxSt5ratioILx1ELx1000000000EEEE");
    Minecraft::Minecraft_getCommands = (MinecraftCommands* (*)(Minecraft*)) hybris_dlsym(handle, "_ZN9Minecraft11getCommandsEv");
    MinecraftCommands::MinecraftCommands_requestCommandExecution = (MCRESULT (*)(MinecraftCommands*, std::unique_ptr<CommandOrigin>, mcpe::string const&, int, bool)) hybris_dlsym(handle, "_ZNK17MinecraftCommands23requestCommandExecutionESt10unique_ptrI13CommandOriginSt14default_deleteIS1_EERKSsib");
    MinecraftCommands::MinecraftCommands_setOutputSender = (void (*)(MinecraftCommands*, std::unique_ptr<CommandOutputSender>)) hybris_dlsym(handle, "_ZN17MinecraftCommands15setOutputSenderESt10unique_ptrI19CommandOutputSenderSt14default_deleteIS1_EE");
    DedicatedServerCommandOrigin::DedicatedServerCommandOrigin_construct = (void (*)(DedicatedServerCommandOrigin*, mcpe::string const&, Minecraft&)) hybris_dlsym(handle, "_ZN28DedicatedServerCommandOriginC2ERKSsR9Minecraft");
    CommandOutputSender::CommandOutputSender_construct = (void (*)(CommandOutputSender*, Automation::AutomationClient&)) hybris_dlsym(handle, "_ZN19CommandOutputSenderC2ERN10Automation16AutomationClientE");
    CommandOutputSender::CommandOutputSender_destruct = (void (*)(CommandOutputSender*)) hybris_dlsym(handle, "_ZN19CommandOutputSenderD2Ev");
    CommandOutputSender::CommandOutputSender_send = (void (*)(CommandOutputSender*, CommandOrigin const&, CommandOutput const&)) hybris_dlsym(handle, "_ZN19CommandOutputSender4sendERK13CommandOriginRK13CommandOutput");
    CommandOutputSender::CommandOutputSender_translate = (std::vector<mcpe::string> (*)(std::vector<mcpe::string> const&)) hybris_dlsym(handle, "_ZN19CommandOutputSender9translateERKSt6vectorISsSaISsEE");
    CommandOutput::CommandOutput_getMessages = (std::vector<CommandOutputMessage> const& (*)(CommandOutput const*)) hybris_dlsym(handle, "_ZNK13CommandOutput11getMessagesEv");
    I18n::I18n_get = (mcpe::string (*)(mcpe::string const&, std::vector<mcpe::string> const&)) hybris_dlsym(handle, "_ZN4I18n3getERKSsRKSt6vectorISsSaISsEE");
    I18n::I18n_loadLanguages = (void (*)(ResourcePackManager&, SkinRepository*, mcpe::string const&)) hybris_dlsym(handle, "_ZN4I18n13loadLanguagesER19ResourcePackManagerP14SkinRepositoryRKSs");
    I18n::I18n_chooseLanguage = (void (*)(mcpe::string const&)) hybris_dlsym(handle, "_ZN4I18n14chooseLanguageERKSs");
    ResourcePackStack::ResourcePackStack_vtable = (void**) hybris_dlsym(handle, "_ZTV11AppPlatform");
    PackInstance::PackInstance_construct = (void (*)(PackInstance*, ResourcePack*, int, bool)) hybris_dlsym(handle, "_ZN12PackInstanceC2EP12ResourcePackib");
    ResourcePackStack::ResourcePackStack_add = (void (*)(ResourcePackStack*, PackInstance const&, ResourcePackRepository const&, bool)) hybris_dlsym(handle, "_ZN17ResourcePackStack3addE12PackInstanceRK22ResourcePackRepositoryb");

    Log::info("Launcher", "Starting server initialization");

    Log::trace("Launcher", "Initializing AppPlatform (vtable)");
    LinuxAppPlatform::initVtable(handle);
    Log::trace("Launcher", "Initializing AppPlatform (create instance)");
    LinuxAppPlatform* platform = new LinuxAppPlatform();
    Log::trace("Launcher", "Initializing AppPlatform (initialize call)");
    platform->initialize();
    Log::debug("Launcher", "AppPlatform initialized successfully");

    Log::trace("Launcher", "Loading whitelist and operator list");
    Whitelist whitelist;
    OpsList ops;
    Log::trace("Launcher", "Initializing Minecraft API classes");
    minecraft::api::Api api;
    api.vtable = (void**) hybris_dlsym(handle, "_ZTVN9minecraft3api3ApiE") + 2;
    api.envPath = cwd;
    api.playerIfaceVtable = (void**) hybris_dlsym(handle, "_ZTVN9minecraft3api15PlayerInterfaceE") + 2;
    api.entityIfaceVtable = (void**) hybris_dlsym(handle, "_ZTVN9minecraft3api15EntityInterfaceE") + 2;
    api.networkIfaceVtable = (void**) hybris_dlsym(handle, "_ZTVN9minecraft3api16NetworkInterfaceE") + 2;
    api.playerInteractionsIfaceVtable = (void**) hybris_dlsym(handle, "_ZTVN9minecraft3api26PlayerInteractionInterfaceE") + 2;

    Log::trace("Launcher", "Setting up level settings");
    LevelSettings levelSettings;
    levelSettings.seed = properties.getInt("level-seed", 0);
    levelSettings.gametype = properties.getInt("gamemode", 0);
    levelSettings.forceGameType = properties.getBool("force-gamemode", false);
    levelSettings.difficulty = properties.getInt("difficulty", 0);
    levelSettings.dimension = 0;
    levelSettings.generator = 0;
    levelSettings.edu = false;
    levelSettings.mpGame = true;
    levelSettings.lanBroadcast = true;
    levelSettings.commandsEnabled = true;
    levelSettings.texturepacksRequired = false;

    Log::trace("Launcher", "Initializing FilePathManager");
    FilePathManager pathmgr (cwd, false);
    Log::trace("Launcher", "Initializing MinecraftEventing (create instance)");
    MinecraftEventing eventing (cwd);
    /*Log::trace("Launcher", "Social::UserManager::CreateUserManager()");
    auto userManager = Social::UserManager::CreateUserManager();*/
    Log::trace("Launcher", "Initializing MinecraftEventing (init call)");
    eventing.init();
    Log::trace("Launcher", "Initializing ResourcePackManager");
    ContentTierManager ctm;
    ResourcePackManager resourcePackManager ([cwd]() {
        return cwd;
    }, ctm);
    Log::trace("Launcher", "Initializing PackManifestFactory");
    PackManifestFactory packManifestFactory (eventing);
    Log::trace("Launcher", "Initializing SkinPackKeyProvider");
    SkinPackKeyProvider skinPackKeyProvider;
    Log::trace("Launcher", "Initializing PackSourceFactory");
    PackSourceFactory packSourceFactory (nullptr);
    Log::trace("Launcher", "Initializing ResourcePackRepository");
    ResourcePackRepository resourcePackRepo (eventing, packManifestFactory, skinPackKeyProvider, &pathmgr, packSourceFactory);
    Log::trace("Launcher", "Adding vanilla resource pack");
    std::unique_ptr<ResourcePackStack> stack (new ResourcePackStack());
    stack->add(PackInstance(resourcePackRepo.vanillaPack, -1, false), resourcePackRepo, false);
    resourcePackManager.setStack(std::move(stack), (ResourcePackStackType) 3, false);
    Log::trace("Launcher", "Initializing NetworkHandler");
    NetworkHandler handler;
    Log::trace("Launcher", "Initializing Automation::AutomationClient");
    DedicatedServerMinecraftApp minecraftApp;
    Automation::AutomationClient aclient (minecraftApp);
    minecraftApp.automationClient = &aclient;
    Log::debug("Launcher", "Initializing ServerInstance");
    ServerInstance instance;
    ServerInstance::ServerInstance_construct(&instance, minecraftApp, whitelist, ops, &pathmgr, std::chrono::duration_cast<std::chrono::duration<long long>>(std::chrono::milliseconds(50)), /* world dir */ properties.getString("level-dir"), /* world name */ properties.getString("level-name"), mcpe::string(), skinPackKeyProvider, mcpe::string(), /* settings */ levelSettings, api, 22, true, /* (query?) port */ properties.getInt("server-port", 19132), /* (maybe not) port */ 19132, properties.getInt("max-players", 20), properties.getBool("online-mode", true), {}, "normal", *mce::UUID::EMPTY, eventing, handler, resourcePackRepo, ctm, resourcePackManager, nullptr, [](mcpe::string const& s) {
        std::cout << "??? " << s.c_str() << "\n";
    });
    Log::trace("Launcher", "Loading language data");
    I18n::loadLanguages(resourcePackManager, nullptr, "en_US");
    resourcePackManager.onLanguageChanged();
    Log::info("Launcher", "Server initialized");

    int flags = fcntl(0, F_GETFL, 0);
    fcntl(0, F_SETFL, flags | O_NONBLOCK);
    char lineBuffer[1024 * 16];
    size_t lineBufferOffset = 0;


    while (true) {
        ssize_t r;
        while ((r = read(0, &lineBuffer[lineBufferOffset], sizeof(lineBuffer) - lineBufferOffset)) > 0)
            lineBufferOffset += r;
        for (size_t i = 0; i < lineBufferOffset; ) {
            if (i == sizeof(lineBuffer) - 1 || lineBuffer[i] == '\n') {
                std::string cmd = std::string(lineBuffer, i);
                memcpy(lineBuffer, &lineBuffer[i + 1], lineBufferOffset - i - 1);
                lineBufferOffset -= i + 1;

                std::unique_ptr<DedicatedServerCommandOrigin> commandOrigin(new DedicatedServerCommandOrigin("Server", *instance.minecraft));
                instance.minecraft->getCommands()->requestCommandExecution(std::move(commandOrigin), cmd, 4, true);
                i = 0;
            } else {
                i++;
            }
        }

        instance.update();
        instance.mainThreadNetworkUpdate_HACK();
        Scheduler::singleton()->processCoroutines(std::chrono::duration_cast<std::chrono::duration<long long>>(std::chrono::milliseconds(50)));
    }
    return 0;
}
