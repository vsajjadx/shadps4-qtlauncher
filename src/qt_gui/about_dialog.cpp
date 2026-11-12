// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDesktopServices>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QTextBrowser>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QImage>
#include <QLabel>
#include <QPixmap>

#include "about_dialog.h"
#include "main_window_themes.h"
#include "ui_about_dialog.h"

AboutDialog::AboutDialog(std::shared_ptr<gui_settings> gui_settings, QWidget* parent)
    : QDialog(parent), ui(new Ui::AboutDialog), m_gui_settings(std::move(gui_settings)) {
    ui->setupUi(this);

    // The author's links: GitHub and Reddit.
    ui->image_1->setToolTip(QStringLiteral("https://github.com/vsajjadx"));
    ui->image_2->setPixmap(RedditLogo());
    ui->image_2->setToolTip(QStringLiteral("https://www.reddit.com/user/Unhappy_Advantage_10/"));
    ui->image_3->setVisible(false);
    ui->image_4->setVisible(false);
    ui->image_5->setVisible(false);

    // Recent: what changed in this build, bundled with it.
    resize(width(), height() + 330);
    setMinimumSize(size());
    setMaximumSize(size());
    QLabel* recent_title = new QLabel(tr("Recent"), this);
    recent_title->setStyleSheet("font-size: 18px; font-weight: bold;");
    recent_title->setGeometry(20, 305, 300, 28);
    QTextBrowser* recent = new QTextBrowser(this);
    recent->setGeometry(20, 335, width() - 40, 290);
    recent->setOpenExternalLinks(true);
    QFile notes(":/recent.md");
    if (notes.open(QIODevice::ReadOnly)) {
        recent->setMarkdown(QString::fromUtf8(notes.readAll()));
    }

    preloadImages();

    ui->image_1->setAttribute(Qt::WA_Hover, true);
    ui->image_2->setAttribute(Qt::WA_Hover, true);
    ui->image_3->setAttribute(Qt::WA_Hover, true);
    ui->image_4->setAttribute(Qt::WA_Hover, true);
    ui->image_5->setAttribute(Qt::WA_Hover, true);

    ui->image_1->installEventFilter(this);
    ui->image_2->installEventFilter(this);
    ui->image_3->installEventFilter(this);
    ui->image_4->installEventFilter(this);
    ui->image_5->installEventFilter(this);

#ifdef FORK_BUNDLE_REPO
    setWindowTitle(tr("About shadPS4 Anti-Crash"));
    ui->shad_title->setText("shadPS4 Anti-Crash");
    ui->shad_title->adjustSize();
#endif
}

// Black Reddit mark on transparent, like the other single-color link icons (the theme code
// inverts them).
QPixmap AboutDialog::RedditLogo() {
    // Same layout as images/github.png: a black disc filling the image with a white figure.
    QImage image(256, 256, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QRectF(0, 0, 256, 256));
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(44, 104, 168, 108)); // head
    p.drawEllipse(QRectF(34, 90, 46, 46));    // ears
    p.drawEllipse(QRectF(176, 90, 46, 46));
    p.drawEllipse(QRectF(154, 34, 36, 36));   // antenna ball
    p.setPen(QPen(Qt::white, 12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath stalk;
    stalk.moveTo(128, 108);
    stalk.lineTo(140, 52);
    stalk.lineTo(166, 54);
    p.drawPath(stalk);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QRectF(88, 138, 26, 26)); // eyes
    p.drawEllipse(QRectF(142, 138, 26, 26));
    p.setPen(QPen(Qt::black, 9, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    QPainterPath smile;
    smile.moveTo(96, 180);
    smile.quadTo(128, 200, 160, 180);
    p.drawPath(smile);
    p.end();
    return QPixmap::fromImage(image);
}

AboutDialog::~AboutDialog() {
    delete ui;
}

void AboutDialog::preloadImages() {
    originalImages[0] = ui->image_1->pixmap().copy();
    originalImages[1] = ui->image_2->pixmap().copy();
    originalImages[2] = ui->image_3->pixmap().copy();
    originalImages[3] = ui->image_4->pixmap().copy();
    originalImages[4] = ui->image_5->pixmap().copy();

    for (int i = 0; i < 5; ++i) {
        QImage image = originalImages[i].toImage();
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QColor color = image.pixelColor(x, y);
                color.setRed(255 - color.red());
                color.setGreen(255 - color.green());
                color.setBlue(255 - color.blue());
                image.setPixelColor(x, y, color);
            }
        }
        invertedImages[i] = QPixmap::fromImage(image);
    }
    updateImagesForCurrentTheme();
}

void AboutDialog::updateImagesForCurrentTheme() {
    Theme currentTheme = static_cast<Theme>(m_gui_settings->GetValue(gui::gen_theme).toInt());
    bool isDarkTheme = (currentTheme == Theme::Dark || currentTheme == Theme::Green ||
                        currentTheme == Theme::Blue || currentTheme == Theme::Violet);
    if (isDarkTheme) {
        ui->image_1->setPixmap(invertedImages[0]);
        ui->image_2->setPixmap(invertedImages[1]);
        ui->image_3->setPixmap(invertedImages[2]);
        ui->image_4->setPixmap(invertedImages[3]);
        ui->image_5->setPixmap(invertedImages[4]);
    } else {
        ui->image_1->setPixmap(originalImages[0]);
        ui->image_2->setPixmap(originalImages[1]);
        ui->image_3->setPixmap(originalImages[2]);
        ui->image_4->setPixmap(originalImages[3]);
        ui->image_5->setPixmap(originalImages[4]);
    }
}

bool AboutDialog::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Enter) {
        if (obj == ui->image_1) {
            if (isDarkTheme()) {
                ui->image_1->setPixmap(originalImages[0]);
            } else {
                ui->image_1->setPixmap(invertedImages[0]);
            }
            applyHoverEffect(ui->image_1);
        } else if (obj == ui->image_2) {
            if (isDarkTheme()) {
                ui->image_2->setPixmap(originalImages[1]);
            } else {
                ui->image_2->setPixmap(invertedImages[1]);
            }
            applyHoverEffect(ui->image_2);
        } else if (obj == ui->image_3) {
            if (isDarkTheme()) {
                ui->image_3->setPixmap(originalImages[2]);
            } else {
                ui->image_3->setPixmap(invertedImages[2]);
            }
            applyHoverEffect(ui->image_3);
        } else if (obj == ui->image_4) {
            if (isDarkTheme()) {
                ui->image_4->setPixmap(originalImages[3]);
            } else {
                ui->image_4->setPixmap(invertedImages[3]);
            }
            applyHoverEffect(ui->image_4);
        } else if (obj == ui->image_5) {
            if (isDarkTheme()) {
                ui->image_5->setPixmap(originalImages[4]);
            } else {
                ui->image_5->setPixmap(invertedImages[4]);
            }
            applyHoverEffect(ui->image_5);
        }
    } else if (event->type() == QEvent::Leave) {
        if (obj == ui->image_1) {
            if (isDarkTheme()) {
                ui->image_1->setPixmap(invertedImages[0]);
            } else {
                ui->image_1->setPixmap(originalImages[0]);
            }
            removeHoverEffect(ui->image_1);
        } else if (obj == ui->image_2) {
            if (isDarkTheme()) {
                ui->image_2->setPixmap(invertedImages[1]);
            } else {
                ui->image_2->setPixmap(originalImages[1]);
            }
            removeHoverEffect(ui->image_2);
        } else if (obj == ui->image_3) {
            if (isDarkTheme()) {
                ui->image_3->setPixmap(invertedImages[2]);
            } else {
                ui->image_3->setPixmap(originalImages[2]);
            }
            removeHoverEffect(ui->image_3);
        } else if (obj == ui->image_4) {
            if (isDarkTheme()) {
                ui->image_4->setPixmap(invertedImages[3]);
            } else {
                ui->image_4->setPixmap(originalImages[3]);
            }
            removeHoverEffect(ui->image_4);
        } else if (obj == ui->image_5) {
            if (isDarkTheme()) {
                ui->image_5->setPixmap(invertedImages[4]);
            } else {
                ui->image_5->setPixmap(originalImages[4]);
            }
            removeHoverEffect(ui->image_5);
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        if (obj == ui->image_1) {
            QDesktopServices::openUrl(QUrl("https://github.com/vsajjadx"));
        } else if (obj == ui->image_2) {
            QDesktopServices::openUrl(QUrl("https://www.reddit.com/user/Unhappy_Advantage_10/"));
        } else if (obj == ui->image_3) {
            QDesktopServices::openUrl(QUrl("https://www.youtube.com/@shadPS4/videos"));
        } else if (obj == ui->image_4) {
            QDesktopServices::openUrl(QUrl("https://ko-fi.com/shadps4"));
        } else if (obj == ui->image_5) {
            QDesktopServices::openUrl(QUrl("https://shadps4.net"));
        }
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

void AboutDialog::applyHoverEffect(QLabel* label) {
    QColor shadowColor = isDarkTheme() ? QColor(0, 0, 0) : QColor(169, 169, 169);
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect;
    shadow->setBlurRadius(5);
    shadow->setXOffset(2);
    shadow->setYOffset(2);
    shadow->setColor(shadowColor);
    label->setGraphicsEffect(shadow);
}

void AboutDialog::removeHoverEffect(QLabel* label) {
    QColor shadowColor = isDarkTheme() ? QColor(50, 50, 50) : QColor(169, 169, 169);
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect;
    shadow->setBlurRadius(3);
    shadow->setXOffset(0);
    shadow->setYOffset(0);
    shadow->setColor(shadowColor);
    label->setGraphicsEffect(shadow);
}

bool AboutDialog::isDarkTheme() const {
    Theme currentTheme = static_cast<Theme>(m_gui_settings->GetValue(gui::gen_theme).toInt());
    return currentTheme == Theme::Dark || currentTheme == Theme::Green ||
           currentTheme == Theme::Blue || currentTheme == Theme::Violet;
}
