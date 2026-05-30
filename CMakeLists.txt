cmake_minimum_required(VERSION 3.20)

project(QtDesktopPet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(QtDesktopPet
    main.cpp
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
    src/character/AnimationClip.h
    src/character/AnimationClip.cpp
    src/character/CharacterPackage.h
    src/character/CharacterPackage.cpp
    src/character/CharacterPackageLoader.h
    src/character/CharacterPackageLoader.cpp
    src/character/FrameAnimator.h
    src/character/FrameAnimator.cpp
    src/config/AIConfig.h
    src/config/AIConfig.cpp
    src/config/AppConfig.h
    src/config/ConfigManager.h
    src/config/ConfigManager.cpp
    src/config/SecretStore.h
    src/config/SecretStore.cpp
    src/state/PetStateMachine.h
    src/state/PetStateMachine.cpp
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
)

target_compile_definitions(QtDesktopPet
    PRIVATE
        PET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(QtDesktopPet
    PRIVATE
        Qt6::Network
        Qt6::Widgets
)

if (WIN32)
    target_compile_definitions(QtDesktopPet PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(QtDesktopPet PRIVATE Crypt32)
endif()
