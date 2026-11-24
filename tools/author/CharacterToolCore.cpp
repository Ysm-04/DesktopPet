#include "CharacterToolCore.h"
#include "../../src/character/CharacterPackageRepository.h"
#include "../../src/character/CharacterPackageLoader.h"
#include "../../src/character/CharacterMediaSpec.h"
#include "../../src/character/WebpAnimationBackend.h"
#include "../../src/character/AlphaVideoSpec.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <cmath>
#include <stdexcept>

namespace
{
struct Failure { int code; QString text; };
void require(bool ok, int code, const QString &text) { if (!ok) throw Failure{code, text}; }
QJsonObject readJson(const QString &path)
{
    QFile f(path); require(f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024, 2, "Cannot read bounded JSON: " + path);
    QJsonParseError e; const auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    require(e.error == QJsonParseError::NoError && doc.isObject(), 2, "Invalid JSON: " + path);
    return doc.object();
}
void writeJson(const QString &path, const QJsonObject &o)
{
    QSaveFile f(path); require(f.open(QIODevice::WriteOnly), 4, "Cannot write: " + path);
    const auto bytes = QJsonDocument(o).toJson();
    require(f.write(bytes) == bytes.size() && f.commit(), 4, "Cannot commit: " + path);
}
QString localPath(const QString &input)
{
    require(!input.isEmpty() && !input.contains(QChar::Null)
        && !input.startsWith("\\\\") && !input.startsWith("//")
        && !input.contains("://") && !input.contains(QRegularExpression("^[a-zA-Z]+:[^/\\\\]")),
        2, "Only local filesystem paths are accepted.");
    const QFileInfo f(input);
    require(f.exists() && !f.isSymLink() && !f.isJunction(), 2, "Input is missing or is a link.");
    // Inspect ancestors too; an ordinary file inside a junction is still external.
    QFileInfo parent(f.absolutePath());
    while (!parent.isRoot())
    {
        require(!parent.isSymLink() && !parent.isJunction(), 2, "Input ancestor is a link.");
        parent = QFileInfo(parent.absolutePath());
    }
    return f.canonicalFilePath();
}
bool nested(const QString &a, const QString &b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0 || a.startsWith(b + '/', Qt::CaseInsensitive);
}
QString tool(const QString &name)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("encoder/" + name + ".exe");
}
QByteArray run(const QString &program, const QStringList &args, const std::function<bool()> &cancelled,
               int code = 4, int timeoutMs = 15 * 60 * 1000)
{
    require(QFileInfo(program).isFile(), 3, "Missing bundled executable: " + program);
    QProcess p;
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif
    p.setProgram(program);
    // The local MinGW build exhibited nondeterministic chroma corruption in
    // swscale's optimized CPU path. Author conversion favors deterministic
    // pixels; this does not affect the pet's independent Qt playback decoder.
    QStringList safeArguments;
    if (QFileInfo(program).baseName().compare("ffmpeg", Qt::CaseInsensitive) == 0)
        safeArguments << "-cpuflags" << "0";
    safeArguments.append(args);
    p.setArguments(safeArguments);
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(QIODevice::ReadOnly);
    require(p.waitForStarted(5000), 3, "Cannot start bundled executable: " + p.errorString());
    QElapsedTimer clock; clock.start();
    QByteArray output, errors;
    while (p.state() != QProcess::NotRunning)
    {
        p.waitForFinished(50);
        output += p.readAllStandardOutput(); errors += p.readAllStandardError();
        if (errors.size() > 65536) errors = errors.right(65536);
        if ((cancelled && cancelled()) || clock.elapsed() > timeoutMs || output.size() > 64 * 1024 * 1024)
        {
            p.kill(); p.waitForFinished(5000);
            throw Failure{cancelled && cancelled() ? 130 : code, "Cancelled, timed out or exceeded output limit."};
        }
    }
    output += p.readAllStandardOutput(); errors += p.readAllStandardError();
    require(p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0, code,
        "Encoder failed: " + QString::fromUtf8(errors.right(4000)));
    return output;
}
void dependencies(const std::function<bool()> &cancelled)
{
    const auto directory = QDir(QCoreApplication::applicationDirPath()).filePath("encoder");
    require(QFileInfo(QDir(directory).filePath("dependency-lock.json")).isFile(), 3, "Missing bundled encoder dependency lock.");
    QJsonObject lock;
    try { lock = readJson(QDir(directory).filePath("dependency-lock.json")); }
    catch (const Failure &f) { throw Failure{3, f.text}; }
    require(lock.value("schemaVersion").toInt() == 1 && lock.value("files").isObject(), 3, "Invalid dependency lock.");
    const auto files = lock["files"].toObject();
    for (const auto &required : {"ffmpeg.exe", "ffprobe.exe", "build-encoder.sh", "sources.json"})
        require(files.contains(required), 3, "Incomplete dependency lock.");
    for (auto it = files.begin(); it != files.end(); ++it)
    {
        require(CharacterMediaSpec::isSafeRelativePath(it.key()), 3, "Unsafe dependency lock path.");
        QFile f(QDir(directory).filePath(it.key()));
        require(f.open(QIODevice::ReadOnly), 3, "Missing encoder material: " + it.key());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        require(hash.addData(&f) && QString::fromLatin1(hash.result().toHex()).compare(it.value().toString(), Qt::CaseInsensitive) == 0,
                3, "Encoder material hash mismatch: " + it.key());
    }
    const auto version = run(tool("ffmpeg"), {"-version"}, cancelled, 3);
    require(version.contains(lock["ffmpegVersion"].toString().toUtf8()), 3, "Unexpected encoder version.");
    run(tool("ffprobe"), {"-version"}, cancelled, 3);
}
QJsonObject probe(const QString &path, const std::function<bool()> &cancelled)
{
    const QFileInfo info(path);
    if (info.isDir())
    {
        if (QFileInfo(QDir(path).filePath("character.json")).exists())
        {
            QString e; const auto p = CharacterPackageLoader::load(path, &e);
            require(e.isEmpty(), 2, e);
            QJsonArray states; for (const auto &s : p.states) states.append(s.name);
            return {{"kind", "package"}, {"states", states}, {"id", p.id}};
        }
        const auto pngs = QDir(path).entryInfoList({"*.png"}, QDir::Files, QDir::Name);
        require(!pngs.isEmpty() && pngs.size() <= 18000, 2, "Expected 1–18000 PNG files.");
        return {{"kind", "png-sequence"}, {"frames", pngs.size()}, {"ordering", "lexicographic; use zero-padded names"}};
    }
    const QString extension = info.suffix().toLower();
    if (extension == "png") return {{"kind", "png"}, {"frames", 1}};
    require(QStringList{"mov", "mp4", "webm", "webp"}.contains(extension), 2, "Unsupported input extension.");
    const auto bytes = run(tool("ffprobe"), {"-v", "error", "-protocol_whitelist", "file,pipe",
        "-show_streams", "-show_format", "-of", "json", path}, cancelled, 2);
    auto o = QJsonDocument::fromJson(bytes).object();
    require(!o["streams"].toArray().isEmpty(), 2, "No readable media streams.");
    o["kind"] = "video"; return o;
}
void copyTree(const QString &source, const QString &destination, const std::function<bool()> &cancelled)
{
    QString error; require(CharacterMediaSpec::preflightPackage(source, &error), 2, error);
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    QDir().mkpath(destination);
    while (it.hasNext())
    {
        it.next(); const auto f = it.fileInfo();
        require(!cancelled || !cancelled(), 130, "Cancelled.");
        const auto target = QDir(destination).filePath(QDir(source).relativeFilePath(f.absoluteFilePath()));
        if (f.isDir()) require(QDir().mkpath(target), 4, "Cannot create directory.");
        else require(QFile::copy(f.absoluteFilePath(), target), 4, "Cannot copy package resource.");
    }
}
double rate(const QString &text)
{
    const auto parts = text.split('/');
    bool ok = false; const double a = parts.value(0).toDouble(&ok);
    if (!ok) return 0;
    const double b = parts.size() == 2 ? parts[1].toDouble() : 1;
    return b > 0 && std::isfinite(a / b) ? a / b : 0;
}
struct Normalized { QString pattern; int frames = 0; double fps = 9; int width = 0, height = 0; };
Normalized normalize(const QString &input, const QJsonObject &recipe, const QString &temporary,
                     const std::function<bool()> &cancelled, CharacterTool::Progress progress,
                     bool firstOnly = false, bool original = false)
{
    QDir().mkpath(temporary);
    const auto info = QFileInfo(input);
    QStringList args{"-v", "error", "-nostdin", "-y", "-protocol_whitelist", "file,pipe"};
    double fps = recipe.value("fps").toDouble(0);
    double sourceFps = recipe.value("sourceFps").toDouble(9);
    require(std::isfinite(sourceFps) && sourceFps >= 1 && sourceFps <= 240, 2, "sourceFps must be 1–240.");
    if (info.isDir() || info.suffix().compare("png", Qt::CaseInsensitive) == 0)
    {
        QStringList files;
        if (info.isDir())
        {
            const auto entries = QDir(input).entryInfoList({"*.png"}, QDir::Files, QDir::Name);
            for (const auto &f : entries) files << localPath(f.absoluteFilePath());
        }
        else files << input;
        require(!files.isEmpty() && files.size() <= 18000, 2, "PNG count outside limits.");
        QDir().mkpath(QDir(temporary).filePath("input"));
        QSize size;
        for (int i = 0; i < files.size(); ++i)
        {
            require(!cancelled || !cancelled(), 130, "Cancelled.");
            QImageReader r(files[i]);
            require(r.size().isValid() && r.size().width() <= 8192 && r.size().height() <= 8192
                && (size.isEmpty() || size == r.size()), 2, "PNG canvas missing, inconsistent or too large.");
            size = r.size();
            require(QFile::copy(files[i], QDir(temporary).filePath(QString("input/%1.png").arg(i, 6, 10, QChar('0')))),
                4, "Cannot stage PNG.");
        }
        if (fps == 0) fps = qMin(30.0, sourceFps);
        args << "-framerate" << QString::number(sourceFps, 'g', 12) << "-i" << QDir(temporary).filePath("input/%06d.png");
    }
    else if (info.suffix().compare("webp", Qt::CaseInsensitive) == 0)
    {
        require(info.size() > 0 && info.size() <= 32 * 1024 * 1024, 2, "WebP input must be within 32MiB.");
        QImageReader reader(input, "webp");
        const int count = reader.imageCount();
        require(count > 0 && count <= 18000, 2, "Invalid WebP frame count.");
        // Timestamp boundaries are cumulative, avoiding per-frame rounding drift.
        QFile list(QDir(temporary).filePath("frames.ffconcat"));
        require(list.open(QIODevice::WriteOnly), 4, "Cannot stage WebP timing.");
        list.write("ffconcat version 1.0\n");
        qint64 totalMs = 0;
        QString lastName;
        for (int i = 0; i < count; ++i)
        {
            require(!cancelled || !cancelled(), 130, "Cancelled.");
            const QImage frame = reader.read();
            const QString name = QString("decoded%1.png").arg(i, 6, 10, QChar('0'));
            require(!frame.isNull() && frame.save(QDir(temporary).filePath(name)), 2, "WebP decoding failed.");
            const int delay = count == 1 ? qMax(1000, reader.nextImageDelay()) : qMax(1, reader.nextImageDelay());
            totalMs += delay;
            require(totalMs <= 300000, 2, "WebP source exceeds 300 seconds.");
            list.write(QString("file %1\noption framerate 1000\nduration %2\n").arg(name).arg(delay / 1000.0, 0, 'f', 6).toUtf8());
            lastName = name;
        }
        // Repeat the final file to establish its cumulative end boundary.
        list.write(QString("file %1\noption framerate 1000\n").arg(lastName).toUtf8());
        list.close();
        if (fps == 0) fps = qBound(1.0, count * 1000.0 / totalMs, 30.0);
        args << "-t" << QString::number(totalMs / 1000.0, 'f', 6);
        // This list is generated exclusively from fixed task-owned filenames;
        // safe=0 permits its millisecond time-base options, never user playlists.
        args << "-f" << "concat" << "-safe" << "0" << "-i" << list.fileName();
    }
    else
    {
        auto p = probe(input, cancelled);
        QJsonObject stream;
        for (const auto &v : p["streams"].toArray())
            if (v.toObject()["codec_type"] == "video") { stream = v.toObject(); break; }
        const auto codec = stream["codec_name"].toString();
        const double duration = p["format"].toObject()["duration"].toString().toDouble();
        require(QStringList{"prores", "vp9", "h264"}.contains(codec)
            && duration > 0 && duration <= 300 && stream["width"].toInt() <= 8192
            && stream["height"].toInt() <= 8192, 2, "Only bounded ProRes, VP9 alpha or H.264 video is supported.");
        const double source = rate(stream["avg_frame_rate"].toString());
        require(source >= 1 && source <= 240, 2, "Unsupported source frame rate.");
        if (fps == 0)
        {
            fps = qMin(source, 30.0);
            if (source > 30 && progress) progress({{"event", "warning"}, {"message", "Source exceeds 30fps; default output is 30fps."}});
        }
        if (codec == "vp9") args << "-c:v" << "libvpx-vp9";
        args << "-i" << input;
    }
    require(fps >= 1 && fps <= 60 && std::isfinite(fps), 2, "Output fps must be 1–60.");
    const int side = recipe.value("maxSide").toInt(512);
    require(side >= 16 && side <= 1024, 2, "maxSide must be 16–1024.");
    const QString packed = recipe.value("sourcePackedLayout").toString();
    QString filter = "format=rgba";
    const auto key = recipe.value("key").toObject();
    if (!original && key.value("enabled").toBool(false))
    {
        const QString color = key.value("color").toString("#00ff00");
        const double similarity = key.value("similarity").toDouble(0.12), blend = key.value("blend").toDouble(0.06);
        const double despill = key.value("despill").toDouble(0.2);
        require(QRegularExpression("^#[0-9a-fA-F]{6}$").match(color).hasMatch()
            && similarity >= 0.01 && similarity <= 1 && blend >= 0 && blend <= 1
            && despill >= 0 && despill <= 1, 2, "Invalid chroma-key settings.");
        filter += QString(",colorkey=0x%1:%2:%3,despill=type=green:mix=%4:alpha=0")
            .arg(color.mid(1)).arg(similarity, 0, 'g', 8).arg(blend, 0, 'g', 8).arg(despill, 0, 'g', 8);
    }
    filter += QString(",fps=%1,scale=w='min(%2,iw)':h='min(%2,ih)':force_original_aspect_ratio=decrease")
        .arg(fps, 0, 'g', 12).arg(side);
    filter += ",pad=ceil(iw/2)*2:ceil(ih/2)*2:0:0:color=black@0,format=rgba";
    const QString pattern = QDir(temporary).filePath("frame%06d.png");
    args << "-an" << "-sn" << "-dn";
    if (packed.isEmpty()) args << "-vf" << filter;
    else
    {
        const QString crops = packed == "left-right"
            ? "[0:v]split[r][a];[r]crop=iw/2:ih:0:0[c];[a]crop=iw/2:ih:iw/2:0,format=gray[m];"
            : "[0:v]split[r][a];[r]crop=iw:ih/2:0:0[c];[a]crop=iw:ih/2:0:ih/2,format=gray[m];";
        args << "-filter_complex" << crops + "[c][m]alphamerge," + filter + "[out]" << "-map" << "[out]";
    }
    if (firstOnly) args << "-frames:v" << "1";
    args << "-threads" << "2" << "-start_number" << "0" << pattern;
    run(tool("ffmpeg"), args, cancelled);
    const auto frames = QDir(temporary).entryInfoList({"frame*.png"}, QDir::Files, QDir::Name);
    require(!frames.isEmpty() && frames.size() <= 18000 && frames.size() / fps <= 300.001, 2, "Output exceeds playback limits.");
    const QSize size = QImageReader(frames.first().absoluteFilePath()).size();
    return {pattern, int(frames.size()), fps, size.width(), size.height()};
}
void fullValidate(const QString &path, const std::function<bool()> &cancelled)
{
    QString error;
    require(CharacterPackageRepository::validatePackageDirectory(path, nullptr, nullptr, &error), 5, error);
    const auto package = CharacterPackageLoader::load(path, &error);
    for (const auto &state : package.states)
    {
        require(!cancelled || !cancelled(), 130, "Cancelled.");
        if (state.mediaType == "frames")
        {
            for (const auto &frame : state.framePaths)
            {
                require(!cancelled || !cancelled(), 130, "Cancelled.");
                QImageReader r(frame); require(!r.read().isNull(), 5, "Bad PNG frame: " + frame);
            }
        }
        else if (state.mediaType == "webp")
        {
            QImageReader r(state.mediaPath, "webp");
            const int count = r.imageCount();
            require(count > 0 && count <= 18000, 5, "Bad WebP frame count.");
            for (int i = 0; i < count; ++i)
            {
                require(!cancelled || !cancelled(), 130, "Cancelled.");
                require(!r.read().isNull(), 5, "Bad WebP frame.");
            }
        }
        else
            run(tool("ffmpeg"), {"-v", "error", "-xerror", "-nostdin", "-protocol_whitelist", "file,pipe",
                "-i", state.mediaPath, "-map", "0:v:0", "-an", "-f", "null", "-"}, cancelled, 5);
    }
}
}

CharacterTool::Result CharacterTool::execute(const QString &command, const QJsonObject &recipe,
                                             Progress progress, const std::function<bool()> &cancelled)
{
    try
    {
        const QStringList allowed{"schemaVersion", "input", "output", "format", "layout", "maxSide", "fps",
            "sourceFps", "state", "states", "id", "displayName", "key", "effects", "closedFrameInput"};
        for (auto it = recipe.begin(); it != recipe.end(); ++it)
            require(allowed.contains(it.key()), 2, "Unknown recipe field: " + it.key());
        require(!recipe.contains("schemaVersion") || recipe.value("schemaVersion").toDouble() == 1, 2, "Unsupported recipe schema.");
        for (const auto &key : {"fps", "sourceFps", "maxSide"})
            require(!recipe.contains(key) || recipe.value(key).isDouble(), 2, QString("Expected numeric field: ") + key);
        require(!recipe.contains("maxSide") || recipe.value("maxSide").toDouble() == recipe.value("maxSide").toInt(),
                2, "maxSide must be an integer.");
        require(!recipe.contains("key") || recipe.value("key").isObject(), 2, "key must be an object.");
        const auto keySettings = recipe.value("key").toObject();
        for (auto it = keySettings.begin(); it != keySettings.end(); ++it)
            require(QStringList{"enabled", "color", "similarity", "blend", "despill"}.contains(it.key()),
                    2, "Unknown key setting: " + it.key());
        require(!keySettings.contains("enabled") || keySettings.value("enabled").isBool(), 2, "key.enabled must be boolean.");
        dependencies(cancelled);
        if (command == "self-check")
        {
            require(QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath("imageformats/qwebp.dll")).isFile(),
                3, "Missing bundled Qt WebP plugin: imageformats/qwebp.dll");
            const auto encoders = run(tool("ffmpeg"), {"-hide_banner", "-encoders"}, cancelled, 3);
            const auto decoders = run(tool("ffmpeg"), {"-hide_banner", "-decoders"}, cancelled, 3);
            require(encoders.contains("libx264") && encoders.contains("libwebp_anim") && encoders.contains("png")
                && decoders.contains("prores") && decoders.contains("libvpx-vp9"), 3, "Required codecs missing.");
            QTemporaryDir test;
            require(test.isValid(), 3, "Cannot create dependency self-check directory.");
            QImage source(32, 32, QImage::Format_RGBA8888); source.fill(Qt::transparent);
            { QPainter painter(&source); painter.fillRect(8, 8, 16, 16, Qt::red); }
            const auto png = test.filePath("input.png");
            const auto webp = test.filePath("probe.webp");
            const auto video = test.filePath("probe.mp4");
            require(source.save(png), 3, "PNG plugin is unavailable.");
            run(tool("ffmpeg"), {"-v", "error", "-nostdin", "-y", "-i", png,
                "-c:v", "libwebp", "-lossless", "1", webp}, cancelled, 3);
            QImageReader reader(webp, "webp");
            const auto decoded = reader.read();
            require(!decoded.isNull() && decoded.pixelColor(0, 0).alpha() == 0
                && decoded.pixelColor(16, 16).red() > 240, 3, "Bundled Qt WebP plugin failed actual decoding.");
            run(tool("ffmpeg"), {"-v", "error", "-nostdin", "-y", "-framerate", "2", "-loop", "1", "-i", png,
                "-frames:v", "2", "-filter_complex",
                "[0:v]split[r][a];[a]alphaextract,format=rgb24[m];[r]format=rgb24[c];[c][m]hstack,format=yuv420p[out]",
                "-map", "[out]", "-an", "-c:v", "libx264", "-crf", "12", "-pix_fmt", "yuv420p", video}, cancelled, 3);
            QString error;
            require(AlphaVideoSpec::inspectFile(video, "left-right", nullptr, &error), 3, error);
            run(tool("ffmpeg"), {"-v", "error", "-xerror", "-nostdin", "-i", video, "-f", "null", "-"}, cancelled, 3);
            return {0, {{"event", "result"}, {"status", "PASS"}, {"dependencies", "verified"},
                {"actualPngWebpVideoConversion", true}, {"qtWebpDecoding", true}}};
        }
        const QString input = localPath(recipe.value("input").toString());
        if (command == "probe") return {0, probe(input, cancelled)};
        if (command == "validate") { fullValidate(input, cancelled); return {0, {{"status", "PASS"}, {"input", input}}}; }
        require(command == "convert" || command == "preview", 2, "Expected probe/convert/validate/self-check/preview.");
        const QString outputArg = recipe.value("output").toString();
        require(QFileInfo(outputArg).isAbsolute() && !outputArg.contains("://") && !outputArg.startsWith("\\\\")
            && !outputArg.startsWith("//"), 2, "Output must be a new local directory.");
        const QString output = QDir::cleanPath(QFileInfo(outputArg).absoluteFilePath());
        require(!QFileInfo::exists(output), 2, "Output already exists; source and previous exports are preserved.");
        const QFileInfo parentInfo(QFileInfo(output).absolutePath());
        require(parentInfo.isDir(), 2, "Output parent must already exist.");
        const QString parent = localPath(parentInfo.absoluteFilePath());
        const QString canonicalOutput = QDir(parent).filePath(QFileInfo(output).fileName());
        require(!nested(canonicalOutput, input) && !nested(input, canonicalOutput), 2, "Source/output directory nesting is forbidden.");
        QTemporaryDir temporary(QDir(parent).filePath(".character-tool-XXXXXX"));
        require(temporary.isValid(), 4, "Cannot create task-owned temporary directory.");
        const auto packagePath = QDir(temporary.path()).filePath("package");
        QDir().mkpath(packagePath);
        const bool existing = QFileInfo(QDir(input).filePath("character.json")).isFile();
        QJsonObject root;
        CharacterPackage sourcePackage;
        QStringList selected;
        if (existing)
        {
            fullValidate(input, cancelled);
            sourcePackage = CharacterPackageLoader::load(input);
            root = readJson(QDir(input).filePath("character.json"));
            copyTree(input, packagePath, cancelled);
            for (const auto &v : recipe.value("states").toArray())
            {
                require(v.isString() && sourcePackage.states.contains(v.toString()) && !selected.contains(v.toString()), 2, "Invalid selected states.");
                selected << v.toString();
            }
            require(!selected.isEmpty(), 2, "Select at least one package state.");
            if (root["schemaVersion"].toInt() == 1)
            {
                auto states = root["states"].toObject();
                for (auto it = states.begin(); it != states.end(); ++it)
                {
                    auto s = it.value().toObject();
                    s["media"] = QJsonObject{{"type", "frames"}, {"path", s.take("path")}, {"fps", s.take("fps")}};
                    it.value() = s;
                }
                root["states"] = states;
            }
            root["schemaVersion"] = 2;
        }
        else
        {
            const auto state = recipe.value("state").toString("idle");
            require(state == "idle", 2, "A new package must initially contain idle; convert selected states of an existing package to extend it.");
            const auto id = recipe.value("id").toString("my-character");
            require(CharacterPackageRepository::isValidCharacterId(id), 2, "Unsafe character id.");
            root = {{"schemaVersion", 2}, {"id", id}, {"displayName", recipe.value("displayName").toString(id)},
                    {"defaultState", "idle"}, {"states", QJsonObject{}}};
            selected << state;
        }
        if (recipe.contains("effects")) root["effects"] = recipe["effects"];
        const QString format = recipe.value("format").toString("webp");
        require(QStringList{"frames", "webp", "alpha_video"}.contains(format), 2, "Unknown output format.");
        const QString layout = recipe.value("layout").toString("left-right");
        require(layout == "left-right" || layout == "top-bottom", 2, "Unknown packed layout.");
        int completed = 0;
        for (const auto &name : selected)
        {
            require(QRegularExpression("^[a-zA-Z0-9_-]{1,64}$").match(name).hasMatch(), 2, "Unsafe state name.");
            QJsonObject options = recipe;
            QString source = input;
            if (existing)
            {
                const auto s = sourcePackage.states[name];
                source = s.mediaType == "frames" ? QFileInfo(s.framePaths.first()).absolutePath() : s.mediaPath;
                if (s.mediaType == "frames") options["sourceFps"] = s.fps;
                if (s.mediaType == "alpha_video") options["sourcePackedLayout"] = s.videoLayout;
            }
            const QString work = QDir(temporary.path()).filePath("work-" + name);
            if (progress) progress({{"event", "progress"}, {"stage", "normalize"}, {"state", name}, {"completed", completed}, {"total", selected.size()}});
            const auto normalized = normalize(source, options, work, cancelled, progress, command == "preview");
            if (command == "preview")
            {
                const auto before = normalize(source, options, work + "-original", cancelled, progress, true, true);
                Q_UNUSED(before);
                require(QFile::copy(QDir(work).filePath("frame000000.png"), QDir(packagePath).filePath("result.png"))
                    && QFile::copy(QDir(work + "-original").filePath("frame000000.png"), QDir(packagePath).filePath("original.png")), 4, "Cannot prepare preview.");
                require(QDir().rename(packagePath, output), 4, "Cannot publish preview.");
                return {0, {{"status", "PASS"}, {"original", QDir(output).filePath("original.png")},
                            {"preview", QDir(output).filePath("result.png")}}};
            }
            QString relative = "generated-" + name;
            for (int suffix = 1; QFileInfo(QDir(packagePath).filePath(relative)).exists(); ++suffix)
                relative = "generated-" + name + "-" + QString::number(suffix);
            QDir().mkpath(QDir(packagePath).filePath(relative));
            QJsonObject media{{"type", format}};
            if (format == "frames")
            {
                require(normalized.frames == 1, 2, "Single PNG output is intended for a static effect character.");
                media["path"] = relative; media["fps"] = 1;
                require(QFile::copy(QDir(work).filePath("frame000000.png"), QDir(packagePath).filePath(relative + "/000000.png")), 4, "Cannot copy frame.");
            }
            else
            {
                const QString file = relative + (format == "webp" ? "/animation.webp" : "/animation.mp4");
                media["path"] = file;
                QStringList args{"-v", "error", "-nostdin", "-y", "-protocol_whitelist", "file,pipe",
                    "-framerate", QString::number(normalized.fps, 'g', 12), "-start_number", "0", "-i", normalized.pattern, "-an"};
                if (format == "webp")
                    args << "-c:v" << "libwebp_anim" << "-lossless" << "1" << "-loop" << "0";
                else
                {
                    media["layout"] = layout;
                    args << "-filter_complex" << QString("[0:v]split[r][a];[a]alphaextract,format=rgb24[m];[r]format=rgb24[c];[c][m]%1,format=yuv420p[out]")
                        .arg(layout == "left-right" ? "hstack" : "vstack")
                        << "-map" << "[out]" << "-c:v" << "libx264" << "-preset" << "medium"
                        << "-crf" << "12" << "-pix_fmt" << "yuv420p" << "-movflags" << "+faststart";
                }
                args << "-threads" << "2" << QDir(packagePath).filePath(file);
                if (progress) progress({{"event", "progress"}, {"stage", "encode"}, {"state", name}});
                run(tool("ffmpeg"), args, cancelled);
            }
            auto states = root["states"].toObject();
            auto state = states[name].toObject();
            state["media"] = media; state.remove("fps"); state.remove("path");
            if (!state.contains("loop")) state["loop"] = true;
            states[name] = state; root["states"] = states;
            if (!existing) root["base"] = QJsonObject{{"width", normalized.width}, {"height", normalized.height}, {"anchorX", 0.5}, {"anchorY", 0}};
            ++completed;
        }
        if (recipe.contains("closedFrameInput") && !recipe.value("closedFrameInput").toString().isEmpty())
        {
            require(format == "frames" && selected.contains("idle"), 2, "Blink requires a static PNG idle output.");
            const auto source = localPath(recipe.value("closedFrameInput").toString());
            const auto idle = root["states"].toObject()["idle"].toObject()["media"].toObject()["path"].toString();
            const auto canvas = QImageReader(QDir(packagePath).filePath(idle + "/000000.png")).size();
            const auto opened = QImageReader(existing ? sourcePackage.states["idle"].framePaths.value(0) : input).size();
            QImageReader reader(source);
            require(reader.size() == opened && canvas.isValid(), 2, "Closed PNG must have the same source canvas as the open PNG.");
            const auto image = reader.read();
            require(!image.isNull(), 2, "Closed PNG cannot be decoded.");
            QImage scaled(canvas, QImage::Format_ARGB32_Premultiplied); scaled.fill(Qt::transparent);
            // Use the same proportional scaling and even transparent padding.
            const int side = recipe.value("maxSide").toInt(512);
            const auto target = image.scaled(QSize(qMin(side, image.width()), qMin(side, image.height())),
                                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter painter(&scaled); painter.drawImage(0, 0, target); painter.end();
            QDir().mkpath(QDir(packagePath).filePath("effects"));
            const auto closed = QDir(packagePath).filePath("effects/idle-closed.png");
            require(!QFileInfo::exists(closed) && scaled.save(closed), 4, "Closed-frame target already exists or cannot be saved.");
        }
        writeJson(QDir(packagePath).filePath("character.json"), root);
        if (progress) progress({{"event", "progress"}, {"stage", "full-validation"}});
        fullValidate(packagePath, cancelled);
        require(!cancelled || !cancelled(), 130, "Cancelled.");
        require(QDir().rename(packagePath, output), 4, "Atomic publication failed; output may have appeared concurrently.");
        return {0, {{"event", "result"}, {"status", "PASS"}, {"output", output}, {"statesConverted", completed},
            {"quality", format == "alpha_video" ? "H.264 CRF12 is lossy; preserve original master."
                : "Lossless relative to normalized/keyed frames; scaling and keying alter the master."}}};
    }
    catch (const Failure &f) { return {f.code, {}, f.text}; }
    catch (const std::exception &e) { return {4, {}, QString::fromUtf8(e.what())}; }
}
