cmake_minimum_required(VERSION 3.20)

project(QtDesktopPet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)

qt_add_executable(QtDesktopPet
    main.cpp
    src/ai/AIDiagnostics.h
    src/ai/AIDiagnostics.cpp
    src/ai/AIProviderFactory.h
    src/ai/AIProviderFactory.cpp
    src/ai/ConversationManager.h
    src/ai/ConversationManager.cpp
    src/ai/ConversationStore.h
    src/ai/ConversationStore.cpp
    src/ai/LLMProvider.h
    src/ai/LLMTypes.h
    src/ai/GoogleGeminiProvider.h
    src/ai/GoogleGeminiProvider.cpp
    src/ai/OpenAICompatibleProvider.h
    src/ai/OpenAICompatibleProvider.cpp
    src/assistant/CommandDispatcher.h
    src/assistant/CommandDispatcher.cpp
    src/assistant/IntentRouter.h
    src/assistant/IntentRouter.cpp
    src/assistant/UserIntent.h
    src/character/AnimationClip.h
    src/character/AnimationClip.cpp
    src/character/CharacterPackage.h
    src/character/CharacterPackage.cpp
    src/character/CharacterPackageLoader.h
    src/character/CharacterPackageLoader.cpp
    src/character/CharacterPackageRepository.h
    src/character/CharacterPackageRepository.cpp
    src/character/FrameAnimator.h
    src/character/FrameAnimator.cpp
    src/config/AIConfig.h
    src/config/AIConfig.cpp
    src/config/AppConfig.h
    src/config/ConfigManager.h
    src/config/ConfigManager.cpp
    src/config/SecretStore.h
    src/config/SecretStore.cpp
    src/fileops/FileBackupManager.h
    src/fileops/FileBackupManager.cpp
    src/fileops/FileOperationManager.h
    src/fileops/FileOperationManager.cpp
    src/fileops/FileOperationTypes.h
    src/fileops/FileSandbox.h
    src/fileops/FileSandbox.cpp
    src/launcher/AppDiscovery.h
    src/launcher/AppDiscovery.cpp
    src/launcher/AppLaunchManager.h
    src/launcher/AppLaunchManager.cpp
    src/launcher/AppLaunchStore.h
    src/launcher/AppLaunchStore.cpp
    src/launcher/AppLaunchTypes.h
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
    src/ui/ChatBubble.h
    src/ui/ChatBubble.cpp
    src/ui/ChatHistoryPanel.h
    src/ui/ChatHistoryPanel.cpp
    src/ui/ChatInputDialog.h
    src/ui/ChatInputDialog.cpp
    src/ui/PetView.h
    src/ui/PetView.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/PetWindow.h
    src/ui/PetWindow.cpp
    src/util/Logger.h
    src/util/Logger.cpp
    src/util/ResourcePaths.h
    src/util/ResourcePaths.cpp
    src/weather/WeatherConfig.h
    src/weather/WeatherManager.h
    src/weather/WeatherManager.cpp
    src/weather/WeatherParser.h
    src/weather/WeatherParser.cpp
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

if (WIN32)
    set_source_files_properties(resources/icons/app_icon.rc
        PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    )
    target_sources(QtDesktopPet PRIVATE resources/icons/app_icon.rc)
    target_compile_definitions(QtDesktopPet PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(QtDesktopPet PRIVATE Crypt32)
endif()
