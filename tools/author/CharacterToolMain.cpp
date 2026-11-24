#include "CharacterToolCore.h"
#include "../../src/character/AlphaVideoSpec.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <atomic>
#ifdef Q_OS_WIN
#include <windows.h>
namespace
{
std::atomic<bool> interrupted{false};
BOOL WINAPI handleConsole(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) { interrupted = true; return TRUE; }
    return FALSE;
}
}
#endif
int main(int argc, char **argv)
{
    AlphaVideoSpec::configureRuntime();
    // No widgets or production configuration are created by the CLI.
    QApplication app(argc, argv);
    QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
    app.setApplicationName("QtDesktopPetCharacterTool");
    app.setApplicationVersion(QStringLiteral(PET_VERSION));
    if (app.arguments().contains("--version"))
    {
        QTextStream(stdout) << app.applicationName() << ' ' << app.applicationVersion() << Qt::endl;
        return 0;
    }
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(handleConsole, TRUE);
#endif
    const auto args = app.arguments();
    auto value = [&](const QString &name) {
        const int index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1] : QString();
    };
    const QString command = args.value(1);
    if (command.isEmpty() || command == "--help")
    {
        QTextStream(stdout) << "QtDesktopPetCharacterTool probe|convert|preview|validate|self-check\n"
            "  --recipe task.json OR --input local-path [--output NEW-directory]\n"
            "  --cancel-file local-token (GUI queue cancellation); Ctrl+C is also supported.\n"
            "Exit: 0 success, 2 input, 3 dependency, 4 conversion, 5 validation, 130 cancellation.\n";
        return command.isEmpty() ? 2 : 0;
    }
    QJsonObject recipe;
    if (!value("--recipe").isEmpty())
    {
        QFile f(value("--recipe"));
        if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024) return 2;
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return 2;
        recipe = doc.object();
    }
    else recipe = {{"input", value("--input")}, {"output", value("--output")}};
    auto emitJson = [](const QJsonObject &o) {
        QTextStream(stdout) << QJsonDocument(o).toJson(QJsonDocument::Compact) << Qt::endl;
    };
    auto cancelled = [&] {
#ifdef Q_OS_WIN
        if (interrupted.load()) return true;
#endif
        return !value("--cancel-file").isEmpty() && QFile::exists(value("--cancel-file"));
    };
    auto result = CharacterTool::execute(command, recipe, emitJson, cancelled);
    if (result.code)
        emitJson({{"event", "result"}, {"status", result.code == 130 ? "CANCELLED" : "FAIL"},
              {"code", result.code}, {"error", result.error}});
    else { result.data["event"] = "result"; emitJson(result.data); }
    return result.code;
}
