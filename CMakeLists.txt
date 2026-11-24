cmake_minimum_required(VERSION 3.20)

project(QtDesktopPet VERSION 0.3.0 LANGUAGES CXX)

# Some Chinese MSVC/CMake combinations misdecode the detected /showIncludes
# prefix. Ninja then misses header dependencies and can mix incompatible ABIs.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^娉ㄦ剰")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()




set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)

qt_add_executable(QtDesktopPet
    main.cpp
    src/agent_bridge/AgentHookManager.h
    src/agent_bridge/AgentHookManager.cpp
    src/agent_bridge/AgentSessionRegistry.h
    src/agent_bridge/AgentSessionRegistry.cpp
    src/agent_bridge/AgentStatusCodec.h
    src/agent_bridge/AgentStatusCodec.cpp
    src/agent_bridge/AgentStatusServer.h
    src/agent_bridge/AgentStatusServer.cpp
    src/agent_bridge/AgentStatusTypes.h
    src/ai/AIDiagnostics.h
    src/ai/AIDiagnostics.cpp
    src/ai/AIProviderFactory.h
    src/ai/AIProviderFactory.cpp
    src/ai/ConversationManager.h
    src/ai/ConversationManager.cpp
    src/ai/ConversationRecallEngine.h
    src/ai/ConversationRecallEngine.cpp
    src/ai/ConversationStore.h
    src/ai/ConversationStore.cpp
    src/ai/LLMProvider.h
    src/ai/LLMTypes.h
    src/ai/LongTermMemoryCommandParser.h
    src/ai/LongTermMemoryCommandParser.cpp
    src/ai/LongTermMemoryExtractor.h
    src/ai/LongTermMemoryExtractor.cpp
    src/ai/LongTermMemoryStore.h
    src/ai/LongTermMemoryStore.cpp
    src/ai/LongTermMemoryTypes.h
    src/ai/GoogleGeminiProvider.h
    src/ai/GoogleGeminiProvider.cpp
    src/ai/OpenAICompatibleProvider.h
    src/ai/OpenAICompatibleProvider.cpp
    src/assistant/CommandDispatcher.h
    src/assistant/CommandDispatcher.cpp
    src/assistant/IntentRouter.h
    src/assistant/IntentRouter.cpp
    src/assistant/UserIntent.h
    src/character/IPetMediaBackend.h
    src/character/AlphaVideoBackend.h
    src/character/AlphaVideoBackend.cpp
    src/character/AlphaVideoSpec.h
    src/character/AlphaVideoSpec.cpp
    src/character/PngSequenceBackend.h
    src/character/PngSequenceBackend.cpp
    src/character/WebpAnimationBackend.h
    src/character/WebpAnimationBackend.cpp
    src/character/CharacterEffects.cpp
    src/character/CharacterEffects.h
    src/ui/CharacterEffectsController.cpp
    src/ui/CharacterEffectsController.h
    src/character/CharacterMediaSpec.h
    src/character/CharacterMediaSpec.cpp
    src/character/AnimationClip.h
    src/character/AnimationClip.cpp
    src/character/CharacterDialogueProvider.h
    src/character/CharacterDialogueProvider.cpp
    src/character/CharacterPackage.h
    src/character/CharacterPackage.cpp
    src/character/CharacterPackageLoader.h
    src/character/CharacterPackageLoader.cpp
    src/character/CharacterPackageRepository.h
    src/character/CharacterPackageRepository.cpp
    src/character/CharacterPersonaStore.h
    src/character/CharacterPersonaStore.cpp
    src/character/FrameAnimator.h
    src/character/FrameAnimator.cpp
    src/config/AIConfig.h
    src/config/AIConfig.cpp
    src/config/AppConfig.h
    src/config/ConfigManager.h
    src/config/ConfigManager.cpp
    src/config/SecretStore.h
    src/config/SecretStore.cpp
    src/notification/NotificationDispatcher.h
    src/notification/NotificationDispatcher.cpp
    src/reminder/ReminderCommandHandler.h
    src/reminder/ReminderCommandHandler.cpp
    src/reminder/ReminderManager.h
    src/reminder/ReminderManager.cpp
    src/reminder/ReminderParser.h
    src/reminder/ReminderParser.cpp
    src/reminder/ReminderSoundPlayer.h
    src/reminder/ReminderSoundPlayer.cpp
    src/reminder/ReminderSoundRepository.h
    src/reminder/ReminderSoundRepository.cpp
    src/reminder/ReminderStore.h
    src/reminder/ReminderStore.cpp
    src/reminder/ReminderTypes.h
    src/reminder/ReminderTypes.cpp
    src/state/PetStateMachine.h
    src/state/PetStateMachine.cpp
    src/system/StartupManager.h
    src/system/StartupManager.cpp
    src/tray/TrayController.h
    src/tray/TrayController.cpp
    src/ui/AgentStatusBar.h
    src/ui/AgentStatusBar.cpp
    src/ui/ChatBubble.h
    src/ui/ChatBubble.cpp
    src/ui/ChatHistoryPanel.h
    src/ui/ChatHistoryPanel.cpp
    src/ui/ChatInputDialog.h
    src/ui/ChatInputDialog.cpp
    src/ui/MarkdownViewHelper.h
    src/ui/MarkdownViewHelper.cpp
    src/ui/PetView.h
    src/ui/PetView.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/PetWindow.h
    src/ui/PetWindow.cpp
    src/util/Logger.h
    src/util/Logger.cpp
    src/util/MarkdownFormatter.h
    src/util/MarkdownFormatter.cpp
    src/util/ResourcePaths.h
    src/util/ResourcePaths.cpp
    src/weather/WeatherConfig.h
    src/weather/WeatherManager.h
    src/weather/WeatherManager.cpp
    src/weather/WeatherLocationResolver.h
    src/weather/WeatherLocationResolver.cpp
    src/weather/WeatherParser.h
    src/weather/WeatherParser.cpp
    src/weather/WeatherProviderCodec.h
    src/weather/WeatherProviderCodec.cpp
    src/weather/WeatherStore.h
    src/weather/WeatherStore.cpp
    src/weather/WeatherSummaryFormatter.h
    src/weather/WeatherSummaryFormatter.cpp
    src/weather/WeatherTypes.h
    src/web/WebCapabilityDetector.h
    src/web/WebCapabilityDetector.cpp
    src/web/WebChatManager.h
    src/web/WebChatManager.cpp
    src/web/WebChatTypes.h
    src/web/WebConfig.h
    src/web/WebProtocolCodec.h
    src/web/WebProtocolCodec.cpp
    src/web/WebStore.h
    src/web/WebStore.cpp
)

target_compile_definitions(QtDesktopPet
    PRIVATE
        PET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(QtDesktopPet
    PRIVATE
        Qt6::Multimedia
        Qt6::Network
        Qt6::Widgets
)

set_target_properties(QtDesktopPet PROPERTIES
    WIN32_EXECUTABLE TRUE
)
set(PET_WEBP_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/webp-runtime/bundle" CACHE PATH
    "Prepared matching WebP runtime bundle (tools/build_webp_plugin.ps1)")
if (WIN32 AND EXISTS "${PET_WEBP_RUNTIME_DIR}/qwebp.dll")
    add_custom_command(TARGET QtDesktopPet POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:QtDesktopPet>/imageformats"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PET_WEBP_RUNTIME_DIR}/qwebp.dll"
            "$<TARGET_FILE_DIR:QtDesktopPet>/imageformats/qwebp.dll"
        VERBATIM
    )
endif()

if (WIN32)
    set_source_files_properties(resources/icons/app_icon.rc
        PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    )
    target_sources(QtDesktopPet PRIVATE resources/icons/app_icon.rc)
    target_compile_definitions(QtDesktopPet PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(QtDesktopPet PRIVATE Crypt32)
endif()

qt_add_executable(QtDesktopPetBridge
    src/agent_bridge/BridgeMain.cpp
    src/agent_bridge/AgentStatusCodec.h
    src/agent_bridge/AgentStatusCodec.cpp
    src/agent_bridge/AgentStatusTypes.h
)

target_link_libraries(QtDesktopPetBridge
    PRIVATE
        Qt6::Core
        Qt6::Network
)

set_target_properties(QtDesktopPetBridge PROPERTIES
    WIN32_EXECUTABLE FALSE
)

if (WIN32)
    target_compile_definitions(QtDesktopPetBridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    # Codex MSIX child processes cannot rely on PATH to locate the Qt runtime.
    add_custom_command(TARGET QtDesktopPetBridge POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>"
            "$<TARGET_FILE:Qt6::Network>"
            "$<TARGET_FILE_DIR:QtDesktopPetBridge>"
        COMMENT "Deploying Qt runtime beside QtDesktopPetBridge"
        VERBATIM
    )
endif()

# Author executables are independent and optional; encoders are packaged only
# with the author kit, never as a runtime dependency of the desktop pet.
option(PET_BUILD_CHARACTER_STUDIO "Build the standalone character author kit" ON)
if(PET_BUILD_CHARACTER_STUDIO)
    add_library(PetCharacterAuthorCore STATIC
        tools/author/CharacterToolCore.cpp
        src/character/CharacterPackage.cpp
        src/character/CharacterPackageLoader.cpp
        src/character/CharacterPackageRepository.cpp
        src/character/CharacterMediaSpec.cpp
        src/character/CharacterEffects.cpp
        src/character/CharacterPersonaStore.cpp
        src/character/AlphaVideoSpec.cpp
        src/character/WebpAnimationBackend.cpp
        src/config/ConfigManager.cpp
        src/config/AIConfig.cpp
        src/util/ResourcePaths.cpp
        src/util/Logger.cpp)
    target_compile_definitions(PetCharacterAuthorCore PRIVATE PET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(PetCharacterAuthorCore PUBLIC Qt6::Widgets Qt6::Multimedia)
    qt_add_executable(QtDesktopPetCharacterTool tools/author/CharacterToolMain.cpp)
    target_link_libraries(QtDesktopPetCharacterTool PRIVATE PetCharacterAuthorCore)
    set_target_properties(QtDesktopPetCharacterTool PROPERTIES WIN32_EXECUTABLE FALSE)
    qt_add_executable(QtDesktopPetCharacterStudio tools/author/CharacterStudioMain.cpp)
    target_link_libraries(QtDesktopPetCharacterStudio PRIVATE PetCharacterAuthorCore)
    set_target_properties(QtDesktopPetCharacterStudio PROPERTIES WIN32_EXECUTABLE TRUE)
    option(PET_BUILD_AUTHOR_QA "Build isolated author GUI test entry" OFF)
    if(PET_BUILD_AUTHOR_QA)
        qt_add_executable(QtDesktopPetCharacterStudioTests tools/author/CharacterStudioMain.cpp)
        target_compile_definitions(QtDesktopPetCharacterStudioTests PRIVATE PET_CHARACTER_STUDIO_QA WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(QtDesktopPetCharacterStudioTests PRIVATE PetCharacterAuthorCore Psapi Setupapi)
        set_target_properties(QtDesktopPetCharacterStudioTests PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
endif()

# Keep runtime --version and Windows file metadata tied to the project version.
if(WIN32)
    configure_file(resources/icons/app_version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app_version.rc" @ONLY)
endif()
foreach(pet_target QtDesktopPet QtDesktopPetBridge QtDesktopPetCharacterTool QtDesktopPetCharacterStudio QtDesktopPetCharacterStudioTests)
    if(TARGET ${pet_target})
        target_compile_definitions(${pet_target} PRIVATE PET_VERSION="${PROJECT_VERSION}")
        if(WIN32)
            target_sources(${pet_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app_version.rc")
        endif()
    endif()
endforeach()
