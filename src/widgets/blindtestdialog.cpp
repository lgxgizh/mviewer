#include "widgets/blindtestdialog.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <chrono>
#include <cmath>
#include <random>

namespace
{
QString describe(const QString &path)
{
    return path.isEmpty() ? QStringLiteral("-") : QFileInfo(path).fileName();
}
} // namespace

BlindTestDialog::BlindTestDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("A/B Blind Test"));
    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    m_pathA = new QComboBox(this);
    m_pathA->setEditable(true);
    m_pathB = new QComboBox(this);
    m_pathB->setEditable(true);
    form->addRow(tr("Source A"), m_pathA);
    form->addRow(tr("Source B"), m_pathB);
    layout->addLayout(form);

    auto *chooseRow = new QHBoxLayout();
    auto *pickA = new QPushButton(tr("Browse A…"), this);
    auto *pickB = new QPushButton(tr("Browse B…"), this);
    chooseRow->addWidget(pickA);
    chooseRow->addWidget(pickB);
    layout->addLayout(chooseRow);

    m_stack = new QStackedWidget(this);
    m_imageLabel = new QLabel(m_stack);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumSize(480, 360);
    m_stack->addWidget(m_imageLabel);
    layout->addWidget(m_stack, 1);

    m_sideLabel = new QLabel(this);
    m_sideLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_sideLabel);

    auto *mosRow = new QHBoxLayout();
    mosRow->addWidget(new QLabel(tr("MOS (1-5)"), this));
    m_mos = new QSlider(Qt::Horizontal, this);
    m_mos->setRange(1, 5);
    m_mos->setValue(3);
    m_mosValue = new QLabel(QStringLiteral("3"), this);
    mosRow->addWidget(m_mos, 1);
    mosRow->addWidget(m_mosValue);
    layout->addLayout(mosRow);

    auto *actions = new QHBoxLayout();
    m_toggle = new QPushButton(tr("Show other side (Space)"), this);
    m_finish = new QPushButton(tr("Reveal && Export"), this);
    m_finish->setProperty("primary", true);
    actions->addWidget(m_toggle);
    actions->addWidget(m_finish);
    layout->addLayout(actions);

    connect(pickA, &QPushButton::clicked, this, &BlindTestDialog::chooseSourceA);
    connect(pickB, &QPushButton::clicked, this, &BlindTestDialog::chooseSourceB);
    connect(m_toggle, &QPushButton::clicked, this, &BlindTestDialog::toggleSide);
    connect(m_finish, &QPushButton::clicked, this, &BlindTestDialog::finish);
    connect(m_mos, &QSlider::valueChanged, this,
            [this](int v)
            {
                m_mosValue->setText(QString::number(v));
                if (m_currentSide == 0)
                    m_score[0] = v;
                else
                    m_score[1] = v;
            });

    // The button advertises Space, so Space must actually work.
    auto *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(spaceShortcut, &QShortcut::activated, this, &BlindTestDialog::toggleSide);

    // A recorded seed keeps the ordering reproducible for the report.
    m_seed = static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count() &
                                       0xFFFFFFFFu);
    applyRandomization();
    refreshView();
}

bool BlindTestDialog::revealed() const
{
    return m_done;
}

void BlindTestDialog::applyRandomization()
{
    std::mt19937 generator(m_seed);
    std::uniform_int_distribution<int> coin(0, 1);
    m_firstSide = coin(generator) == 0 ? 0 : 1; // 0 = A first, 1 = B first
    m_secondSide = 1 - m_firstSide;
    m_currentSide = 0;
    // The slider's default IS the first side's score: leaving the scores at 0
    // recorded "MOS 0" for a side the user never changed (setValue on an
    // already-3 slider emits no signal).
    m_score[0] = m_score[1] = m_mos->value();
    m_mos->setValue(m_score[0]);
    // Picking different sources starts a new blind run: the previous reveal
    // must not carry over.
    m_done = false;
}

void BlindTestDialog::refreshView()
{
    const bool hasBoth = !m_sourceA.isEmpty() && !m_sourceB.isEmpty();
    m_toggle->setEnabled(hasBoth);
    m_finish->setEnabled(hasBoth);

    const QString path = (m_currentSide == 0) ? (m_firstSide == 0 ? m_sourceA : m_sourceB)
                                              : (m_secondSide == 0 ? m_sourceA : m_sourceB);
    if (path.isEmpty())
    {
        m_imageLabel->setPixmap(QPixmap());
        m_sideLabel->setText(tr("Choose two sources to begin"));
        return;
    }
    const QPixmap pixmap(path);
    if (pixmap.isNull())
    {
        m_imageLabel->setPixmap(QPixmap());
        m_sideLabel->setText(tr("Cannot read %1").arg(describe(path)));
        return;
    }
    m_imageLabel->setPixmap(
        pixmap.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_sideLabel->setText(m_done ? tr("Revealed: %1").arg(describe(path))
                                : tr("Side %1 of 2 — identity hidden").arg(m_currentSide + 1));
}

void BlindTestDialog::chooseSourceA()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Source A"), m_sourceA);
    if (path.isEmpty())
        return;
    m_sourceA = path;
    m_pathA->setCurrentText(path);
    applyRandomization();
    refreshView();
}

void BlindTestDialog::chooseSourceB()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Source B"), m_sourceB);
    if (path.isEmpty())
        return;
    m_sourceB = path;
    m_pathB->setCurrentText(path);
    applyRandomization();
    refreshView();
}

void BlindTestDialog::setSources(const QString &a, const QString &b)
{
    m_sourceA = a;
    m_sourceB = b;
    m_pathA->setCurrentText(a);
    m_pathB->setCurrentText(b);
    applyRandomization();
    refreshView();
}

void BlindTestDialog::toggleSide()
{
    if (m_sourceA.isEmpty() || m_sourceB.isEmpty())
        return;
    m_currentSide = 1 - m_currentSide;
    m_mos->setValue(m_currentSide == 0 ? (m_score[0] > 0 ? m_score[0] : 3)
                                       : (m_score[1] > 0 ? m_score[1] : 3));
    refreshView();
}

void BlindTestDialog::finish()
{
    m_done = true;
    refreshView();

    const int scoreA = (m_firstSide == 0) ? m_score[0] : m_score[1];
    const int scoreB = (m_firstSide == 0) ? m_score[1] : m_score[0];
    const QString summary = tr("A: %1 (MOS %2)   B: %3 (MOS %4)")
                                .arg(describe(m_sourceA))
                                .arg(scoreA)
                                .arg(describe(m_sourceB))
                                .arg(scoreB);
    QMessageBox::information(this, tr("Blind test result"), summary);
    exportResults();
}

void BlindTestDialog::exportResults()
{
    const QString destination = QFileDialog::getSaveFileName(this, tr("Export blind test result"),
                                                             QString(), tr("JSON (*.json)"));
    if (destination.isEmpty())
        return;
    const int scoreA = (m_firstSide == 0) ? m_score[0] : m_score[1];
    const int scoreB = (m_firstSide == 0) ? m_score[1] : m_score[0];
    QJsonObject root;
    root.insert(QStringLiteral("seed"), static_cast<double>(m_seed));
    root.insert(QStringLiteral("sourceA"), m_sourceA);
    root.insert(QStringLiteral("sourceB"), m_sourceB);
    root.insert(QStringLiteral("mosA"), scoreA);
    root.insert(QStringLiteral("mosB"), scoreB);
    root.insert(QStringLiteral("firstSideWasA"), m_firstSide == 0);
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(this, tr("Blind test"), tr("Could not write %1").arg(destination));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}
