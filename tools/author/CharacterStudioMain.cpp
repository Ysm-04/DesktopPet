#include "../../src/character/AlphaVideoSpec.h"
#include "../../src/character/CharacterPackageLoader.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QFile>
#include <QDir>
#include <QPainter>
#include <QQueue>
#include <QScrollArea>
#ifdef PET_CHARACTER_STUDIO_QA
#include "../../internal/qa/include/WindowPerformanceMetrics.h"
#include <QElapsedTimer>
#endif

namespace
{
class Studio : public QWidget
{
    struct Job { QString command, recipe, cancel; QJsonObject task; };
    QLineEdit *m_input, *m_output, *m_id, *m_closed;
    QComboBox *m_format, *m_layout, *m_background;
    QSpinBox *m_side, *m_fps, *m_sourceFps;
    QDoubleSpinBox *m_similarity, *m_blend, *m_despill;
    QCheckBox *m_key, *m_breathe, *m_sway, *m_badge, *m_blink;
    QPushButton *m_color;
    QListWidget *m_states, *m_queueView;
    QLabel *m_before, *m_after, *m_status;
    QTextEdit *m_log;
    QProgressBar *m_progress;
    QProcess m_process;
    QTemporaryDir m_temporary;
    QQueue<Job> m_queue;
    Job m_current;
    QByteArray m_pending;
    QJsonObject m_result;
    QString m_keyColor = "#00ff00";
    int m_serial = 0;
    QImage m_originalImage, m_resultImage;
public:
    Studio()
    {
        setWindowTitle(QStringLiteral("角色工坊 · QtDesktopPet"));
#ifdef Q_OS_WIN
        m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
            arguments->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif
        resize(960, 760);
        setStyleSheet("QWidget { font-size: 10pt; } QGroupBox { margin-top: 12px; padding-top: 14px; }"
                      "QPushButton { padding: 6px 12px; } QLineEdit,QComboBox { min-height: 26px; }");
        auto *root = new QVBoxLayout(this);
        auto *heading = new QLabel(QStringLiteral("角色工坊\n选择素材 → 设置输出 → 对照预览 → 加入生成队列"), this);
        heading->setStyleSheet("font-size: 17pt; font-weight: 600; padding: 8px;");
        root->addWidget(heading);
        auto *pages = new QStackedWidget(this);
        auto addPage = [pages](QWidget *page) {
            auto *scroll = new QScrollArea; scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(page); pages->addWidget(scroll);
        };
        auto *navigation = new QHBoxLayout;
        const QStringList steps{QStringLiteral("1 · 素材"), QStringLiteral("2 · 输出与轻动效"), QStringLiteral("3 · 预览与生成")};
        for (int i = 0; i < steps.size(); ++i)
        {
            auto *b = new QPushButton(steps[i], this); navigation->addWidget(b);
            connect(b, &QPushButton::clicked, this, [pages, i] { pages->setCurrentIndex(i); });
        }
        root->addLayout(navigation); root->addWidget(pages);
        auto *sourcePage = new QWidget; auto *sourceForm = new QFormLayout(sourcePage);
        m_input = new QLineEdit; m_output = new QLineEdit; m_id = new QLineEdit("my-character");
        auto *choose = new QPushButton(QStringLiteral("选择文件"));
        auto *chooseDirectory = new QPushButton(QStringLiteral("选择序列 / 角色目录"));
        auto *sourceRow = new QHBoxLayout; sourceRow->addWidget(m_input); sourceRow->addWidget(choose); sourceRow->addWidget(chooseDirectory);
        sourceForm->addRow(QStringLiteral("本地素材"), sourceRow);
        connect(choose, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择透明原片或绿幕视频"), {}, "Media (*.png *.mov *.mp4 *.webm *.webp)");
            if (!path.isEmpty()) { m_input->setText(path); refreshStates(); }
        });
        connect(chooseDirectory, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 PNG 序列或已有角色包"));
            if (!path.isEmpty()) { m_input->setText(path); refreshStates(); }
        });
        connect(m_input, &QLineEdit::editingFinished, this, [this] { refreshStates(); });
        auto *target = new QPushButton(QStringLiteral("选择父目录"));
        auto *targetRow = new QHBoxLayout; targetRow->addWidget(m_output); targetRow->addWidget(target);
        connect(target, &QPushButton::clicked, this, [this] {
            const auto parent = QFileDialog::getExistingDirectory(this, QStringLiteral("输出将新建子目录，不覆盖已有文件"));
            if (!parent.isEmpty()) m_output->setText(QDir(parent).filePath(m_id->text()));
        });
        sourceForm->addRow(QStringLiteral("新输出目录"), targetRow);
        sourceForm->addRow(QStringLiteral("新角色 ID"), m_id);
        m_states = new QListWidget; m_states->setSelectionMode(QAbstractItemView::MultiSelection);
        sourceForm->addRow(QStringLiteral("已有包：选择要转换的状态"), m_states);
        auto *hint = new QLabel(QStringLiteral("支持 PNG 序列、ProRes 4444 MOV、VP9 Alpha WebM、绿幕 H.264。\n"
            "PNG 按文件名排序，请使用 000001.png 形式。已有包保留人设、台词、锚点和未选状态。\n"
            "保留原始母版；工坊不做 AI 补帧，也不读取桌宠个人配置。"));
        hint->setWordWrap(true); sourceForm->addRow(hint); addPage(sourcePage);
        auto *options = new QWidget; auto *form = new QFormLayout(options);
        m_format = new QComboBox; m_format->addItem("Animated WebP · 无损编码", "webp");
        m_format->addItem("Alpha-Matte MP4 · H.264 CRF12", "alpha_video"); m_format->addItem("单 PNG + 轻动效", "frames");
        form->addRow(QStringLiteral("输出类型"), m_format);
        m_layout = new QComboBox; m_layout->addItem("左右拼接", "left-right"); m_layout->addItem("上下拼接", "top-bottom");
        form->addRow(QStringLiteral("视频拼接"), m_layout);
        m_side = new QSpinBox; m_side->setRange(16, 1024); m_side->setValue(512);
        form->addRow(QStringLiteral("最大逻辑边（保持比例）"), m_side);
        m_fps = new QSpinBox; m_fps->setRange(0, 60); m_fps->setSpecialValueText(QStringLiteral("自动（源 ≤30fps）"));
        form->addRow(QStringLiteral("输出帧率"), m_fps);
        m_sourceFps = new QSpinBox; m_sourceFps->setRange(1, 240); m_sourceFps->setValue(9);
        form->addRow(QStringLiteral("PNG 序列源帧率"), m_sourceFps);
        m_key = new QCheckBox(QStringLiteral("启用绿幕抠像（预览与导出使用同一组参数）")); form->addRow(m_key);
        m_color = new QPushButton(m_keyColor);
        connect(m_color, &QPushButton::clicked, this, [this] {
            const auto color = QColorDialog::getColor(QColor(m_keyColor), this, QStringLiteral("选择背景颜色"));
            if (color.isValid()) { m_keyColor = color.name(); m_color->setText(m_keyColor); }
        });
        form->addRow(QStringLiteral("背景取色"), m_color);
        auto number = [](double value, double min) { auto *s = new QDoubleSpinBox; s->setRange(min, 1); s->setDecimals(3); s->setSingleStep(0.01); s->setValue(value); return s; };
        m_similarity = number(0.12, 0.01); m_blend = number(0.06, 0); m_despill = number(0.2, 0);
        form->addRow(QStringLiteral("相似度"), m_similarity); form->addRow(QStringLiteral("过渡"), m_blend); form->addRow(QStringLiteral("去绿边"), m_despill);
        m_breathe = new QCheckBox(QStringLiteral("idle 呼吸（0.8%，3.6 秒）")); m_sway = new QCheckBox(QStringLiteral("拖拽摆动（±4°）"));
        m_badge = new QCheckBox(QStringLiteral("状态角标")); m_blink = new QCheckBox(QStringLiteral("静态 idle 眨眼（完整闭眼 PNG）"));
        form->addRow(m_breathe); form->addRow(m_sway); form->addRow(m_badge); form->addRow(m_blink);
        m_closed = new QLineEdit;
        auto *closedRow = new QHBoxLayout; auto *closedButton = new QPushButton(QStringLiteral("选择闭眼图"));
        closedRow->addWidget(m_closed); closedRow->addWidget(closedButton); form->addRow(closedRow);
        connect(closedButton, &QPushButton::clicked, this, [this] {
            auto path = QFileDialog::getOpenFileName(this, QStringLiteral("与睁眼图同画布的完整 PNG"), {}, "PNG (*.png)");
            if (!path.isEmpty()) m_closed->setText(path);
        });
        addPage(options);
        auto *previewPage = new QWidget; auto *previewLayout = new QVBoxLayout(previewPage);
        auto *previewControls = new QHBoxLayout;
        auto *preview = new QPushButton(QStringLiteral("更新对照预览"));
        m_background = new QComboBox; m_background->addItems({QStringLiteral("透明棋盘"), QStringLiteral("浅色"), QStringLiteral("深色")});
        previewControls->addWidget(preview); previewControls->addWidget(m_background); previewLayout->addLayout(previewControls);
        auto *imageTitles = new QHBoxLayout;
        imageTitles->addWidget(new QLabel(QStringLiteral("原始素材 · 首帧")));
        imageTitles->addWidget(new QLabel(QStringLiteral("处理结果 · 首帧")));
        previewLayout->addLayout(imageTitles);
        auto *images = new QHBoxLayout;
        m_before = new QLabel(QStringLiteral("原始首帧")); m_after = new QLabel(QStringLiteral("结果首帧"));
        for (auto *label : {m_before, m_after}) { label->setMinimumSize(360, 240); label->setAlignment(Qt::AlignCenter); images->addWidget(label); }
        previewLayout->addLayout(images);
        auto *previewHint = new QLabel(QStringLiteral("对照为实际首帧处理结果，绿幕复杂边缘请自行检查。WebP 的无损是相对缩放/抠像后的帧。\n视频默认静音；预览不代替在桌宠中检查完整动作和边缘。"));
        previewHint->setWordWrap(true); previewLayout->addWidget(previewHint);
        connect(preview, &QPushButton::clicked, this, [this] { enqueue("preview"); });
        connect(m_background, &QComboBox::currentIndexChanged, this, [this] { drawPreviews(); });
        auto *generate = new QPushButton(QStringLiteral("加入生成队列"));
        connect(generate, &QPushButton::clicked, this, [this] { enqueue("convert"); });
        previewLayout->addWidget(generate);
        m_queueView = new QListWidget; previewLayout->addWidget(m_queueView);
        addPage(previewPage);
        m_progress = new QProgressBar; m_progress->setRange(0, 1); root->addWidget(m_progress);
        auto *bottom = new QHBoxLayout; m_status = new QLabel(QStringLiteral("就绪 · 所有输出均写入新目录"));
        auto *cancel = new QPushButton(QStringLiteral("取消当前 / 清空等待队列"));
        bottom->addWidget(m_status, 1); bottom->addWidget(cancel); root->addLayout(bottom);
        m_log = new QTextEdit; m_log->setReadOnly(true); m_log->setMaximumHeight(95);
        m_log->document()->setMaximumBlockCount(500); m_log->hide();
        auto *diagnostics = new QPushButton(QStringLiteral("显示诊断记录"));
        connect(diagnostics, &QPushButton::clicked, this, [this, diagnostics] {
            m_log->setVisible(!m_log->isVisible());
            diagnostics->setText(m_log->isVisible() ? QStringLiteral("收起诊断记录") : QStringLiteral("显示诊断记录"));
        });
        root->addWidget(diagnostics); root->addWidget(m_log);
        connect(cancel, &QPushButton::clicked, this, [this] { cancelAll(); });
        connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
            m_pending += m_process.readAllStandardOutput();
            while (m_pending.contains('\n'))
            {
                const int end = m_pending.indexOf('\n');
                const auto line = m_pending.left(end); m_pending.remove(0, end + 1);
                const auto o = QJsonDocument::fromJson(line).object();
                if (o["event"] == "result") m_result = o;
                else if (o["event"] == "progress") m_status->setText(o["stage"].toString() + " · " + o["state"].toString());
                m_log->insertPlainText(QString::fromUtf8(line) + '\n');
            }
        });
        connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
            m_log->insertPlainText(QString::fromUtf8(m_process.readAllStandardError()));
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) { m_status->setText(QStringLiteral("CLI 无法启动，请检查完整作者包")); finish(-1); }
        });
        connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
#ifdef PET_CHARACTER_STUDIO_QA
            ++m_qaFinished;
            if (code == 0 && status == QProcess::NormalExit) ++m_qaSucceeded;
            else if (code == 130 && status == QProcess::NormalExit) ++m_qaCancelled;
            else ++m_qaFailed;
#endif
            finish(status == QProcess::NormalExit ? code : -1);
        });
#ifdef PET_CHARACTER_STUDIO_QA
        if (QCoreApplication::arguments().contains("--qa-recipe"))
            QTimer::singleShot(200, this, [this] { startQa(); });
#endif
    }
private:
#ifdef PET_CHARACTER_STUDIO_QA
    int m_qaFinished = 0, m_qaSucceeded = 0, m_qaCancelled = 0, m_qaFailed = 0;
    QJsonObject m_qaRecipe;
    QElapsedTimer m_qaClock;
    QTimer m_qaActions, m_qaSampler, m_qaHeartbeat;
    QJsonArray m_qaSamples, m_qaBeats;
    qint64 m_qaLastBeat = 0;
    int m_qaCycles = 0;
    void startQa()
    {
        const auto args = QCoreApplication::arguments();
        auto value = [&](const QString &key) { const int i = args.indexOf(key); return args.value(i + 1); };
        QFile recipeFile(value("--qa-recipe"));
        if (!recipeFile.open(QIODevice::ReadOnly)) { qApp->exit(2); return; }
        m_qaRecipe = QJsonDocument::fromJson(recipeFile.readAll()).object();
        const int duration = value("--qa-duration-ms").toInt();
        if (duration < 5000 || duration > 600000 || m_qaRecipe.isEmpty()) { qApp->exit(2); return; }
        const QString report = value("--qa-report");
        m_input->setText(m_qaRecipe["input"].toString());
        m_output->setText(m_temporary.filePath("initial-export")); refreshStates();
        findChild<QStackedWidget *>()->setCurrentIndex(2);
        m_qaClock.start();
        connect(&m_qaHeartbeat, &QTimer::timeout, this, [this] {
            const auto now = m_qaClock.elapsed(); m_qaBeats.append(double(now - m_qaLastBeat)); m_qaLastBeat = now;
        });
        m_qaHeartbeat.start(100);
        connect(&m_qaSampler, &QTimer::timeout, this, [this] {
            auto row = WindowPerformance::processSnapshot(); row["elapsedMs"] = double(m_qaClock.elapsed());
            row["queued"] = m_queue.size(); row["busy"] = m_process.state() != QProcess::NotRunning;
            m_qaSamples.append(row);
        });
        m_qaSampler.start(1000);
        connect(&m_qaActions, &QTimer::timeout, this, [this, duration] {
            if (m_qaClock.elapsed() > duration - 5000) { cancelAll(); return; }
            if (m_process.state() != QProcess::NotRunning || !m_queue.isEmpty()) return;
            m_background->setCurrentIndex(m_qaCycles % 3);
            enqueue("preview");
            m_output->setText(m_temporary.filePath(QString("export-%1").arg(m_qaCycles)));
            m_format->setCurrentIndex(m_qaCycles % 2);
            enqueue("convert");
            // Every other cycle starts a real task with an existing cancellation
            // token. The CLI checks cancellation while its child is running.
            if (m_qaCycles % 2 == 1 && !m_queue.isEmpty())
            {
                QFile token(m_queue.back().cancel); token.open(QIODevice::WriteOnly);
            }
            ++m_qaCycles;
        });
        m_qaActions.start(1000);
        // Timer ends the actual timed session, then polls only to let cancellation
        // finish, so no encoder is abandoned when the GUI exits.
        QTimer::singleShot(duration, this, [this, report] {
            m_qaActions.stop(); cancelAll();
            auto *settle = new QTimer(this);
            connect(settle, &QTimer::timeout, this, [this, report, settle] {
                if (m_process.state() != QProcess::NotRunning) return;
                settle->stop(); m_qaSampler.stop(); m_qaHeartbeat.stop();
                const bool pass = m_qaFailed == 0 && m_qaSucceeded >= 2 && m_qaCancelled >= 1;
                QJsonObject result{{"status", pass ? "PASS" : "FAIL"}, {"actualMs", double(m_qaClock.elapsed())},
                    {"finished", m_qaFinished}, {"succeeded", m_qaSucceeded}, {"cancelled", m_qaCancelled},
                    {"failed", m_qaFailed}, {"samples", m_qaSamples}, {"heartbeatIntervalsMs", m_qaBeats},
                    {"childRunning", m_process.state() != QProcess::NotRunning}};
                QFile f(report); if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(result).toJson());
                grab().save(report + ".png");
                qApp->exit(pass ? 0 : 1);
            });
            settle->start(100);
        });
    }
#endif
    void refreshStates()
    {
        m_states->clear();
        if (QFileInfo(QDir(m_input->text()).filePath("character.json")).isFile())
        {
            QString error; auto package = CharacterPackageLoader::load(m_input->text(), &error);
            if (!error.isEmpty()) { m_status->setText(error); return; }
            m_states->addItems(package.states.keys());
            for (int i = 0; i < m_states->count(); ++i) if (m_states->item(i)->text() == "idle") m_states->item(i)->setSelected(true);
            m_id->setText(package.id);
        }
    }
    QJsonObject recipe()
    {
        QJsonArray states; for (auto *item : m_states->selectedItems()) states.append(item->text());
        QJsonObject o{{"schemaVersion", 1}, {"input", m_input->text()}, {"output", m_output->text()},
            {"format", m_format->currentData().toString()}, {"layout", m_layout->currentData().toString()},
            {"maxSide", m_side->value()}, {"fps", m_fps->value()}, {"sourceFps", m_sourceFps->value()},
            {"id", m_id->text()}, {"states", states}, {"closedFrameInput", m_closed->text()},
            {"key", QJsonObject{{"enabled", m_key->isChecked()}, {"color", m_keyColor},
                {"similarity", m_similarity->value()}, {"blend", m_blend->value()}, {"despill", m_despill->value()}}}};
        QJsonObject effects;
        if (m_breathe->isChecked()) effects["breathing"] = QJsonObject{{"states", QJsonArray{"idle"}}, {"amplitude", 0.008}, {"periodMs", 3600}};
        if (m_sway->isChecked()) effects["dragSway"] = QJsonObject{{"maxAngleDeg", 4}};
        if (m_badge->isChecked()) effects["statusBadge"] = QJsonObject{{"enabled", true}};
        if (m_blink->isChecked()) effects["blink"] = QJsonObject{{"state", "idle"}, {"closedFrame", "effects/idle-closed.png"},
            {"intervalMs", QJsonArray{4000, 8000}}, {"holdMs", 120}};
        if (!effects.isEmpty()) o["effects"] = effects;
        return o;
    }
    void enqueue(const QString &command)
    {
        auto task = recipe();
        if (m_input->text().isEmpty() || (command == "convert" && m_output->text().isEmpty()))
        { m_status->setText(QStringLiteral("请填写素材和新输出目录")); return; }
        const QString prefix = m_temporary.filePath(QString::number(++m_serial));
        if (command == "preview") task["output"] = prefix + "-preview";
        Job job{command, prefix + ".json", prefix + ".cancel", task};
        QFile f(job.recipe);
        if (!f.open(QIODevice::WriteOnly)) { m_status->setText(QStringLiteral("无法写入临时配方")); return; }
        f.write(QJsonDocument(task).toJson()); f.close();
        m_queue.enqueue(job);
        m_queueView->addItem(command + " · " + task["input"].toString() + " → " + task["output"].toString());
        while (m_queueView->count() > 100) delete m_queueView->takeItem(0);
        startNext();
    }
    void startNext()
    {
        if (m_process.state() != QProcess::NotRunning || m_queue.isEmpty() || !m_current.recipe.isEmpty()) return;
        m_current = m_queue.dequeue(); m_result = {}; m_pending.clear(); m_progress->setRange(0, 0);
        m_process.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath("QtDesktopPetCharacterTool.exe"));
        m_process.setArguments({m_current.command, "--recipe", m_current.recipe, "--cancel-file", m_current.cancel});
        m_process.start(QIODevice::ReadOnly);
    }
    void finish(int code)
    {
        if (m_current.recipe.isEmpty()) return;
        if (code == 0 && m_current.command == "preview")
        {
            m_originalImage.load(m_result["original"].toString()); m_resultImage.load(m_result["preview"].toString()); drawPreviews();
        }
        m_status->setText(code == 0 ? QStringLiteral("完成 · ") + m_result["output"].toString()
            : code == 130 ? QStringLiteral("已取消，源素材未改动") : QStringLiteral("失败 · ") + m_result["error"].toString());
        m_progress->setRange(0, 1); m_progress->setValue(code == 0 ? 1 : 0);
        m_current = {};
        QTimer::singleShot(0, this, [this] { startNext(); });
    }
    void drawPreviews()
    {
        auto render = [&](QLabel *label, const QImage &source) {
            if (source.isNull()) return;
            QPixmap canvas(label->size()); canvas.fill(m_background->currentIndex() == 2 ? QColor("#18202b") : QColor("#f0f3f7"));
            QPainter p(&canvas);
            if (m_background->currentIndex() == 0)
                for (int y = 0; y < canvas.height(); y += 16)
                    for (int x = 0; x < canvas.width(); x += 16)
                        if ((x / 16 + y / 16) % 2) p.fillRect(x, y, 16, 16, QColor("#c8ced8"));
            const auto image = source.scaled(canvas.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage((canvas.width() - image.width()) / 2, (canvas.height() - image.height()) / 2, image);
            p.end(); label->setPixmap(canvas);
        };
        render(m_before, m_originalImage); render(m_after, m_resultImage);
    }
    void cancelAll()
    {
        m_queue.clear(); m_queueView->clear();
        if (!m_current.cancel.isEmpty()) { QFile f(m_current.cancel); f.open(QIODevice::WriteOnly); }
    }
    void closeEvent(QCloseEvent *event) override
    {
        if (m_process.state() != QProcess::NotRunning)
        {
            cancelAll(); m_status->setText(QStringLiteral("正在取消编码，请稍后关闭窗口")); event->ignore();
        }
        else event->accept();
    }
};
}
int main(int argc, char **argv)
{
    AlphaVideoSpec::configureRuntime();
    QApplication app(argc, argv);
    QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
    app.setApplicationName("QtDesktopPetCharacterStudio");
    app.setApplicationVersion(QStringLiteral(PET_VERSION));
    if (app.arguments().contains("--version"))
    {
        QTextStream(stdout) << app.applicationName() << ' ' << app.applicationVersion() << Qt::endl;
        return 0;
    }
    Studio studio; studio.show();
    return app.exec();
}
