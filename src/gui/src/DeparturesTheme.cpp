/*
 * Inputgate -- mouse, keyboard, clipboard and file sharing utility
 * Copyright (C) 2026 The Inputgate Developers
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "DeparturesTheme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGroupBox>
#include <QProxyStyle>
#include <QPushButton>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QPalette>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <functional>

namespace {

DeparturesTheme* s_instance = nullptr;

// tokens.css, light
DeparturesTheme::Tokens lightTokens()
{
    DeparturesTheme::Tokens t;
    t.bg = QColor("#e9ecef");
    t.surface = QColor("#ffffff");
    t.surface2 = QColor("#dde1e6");
    t.text = QColor("#0f1319");
    t.muted = QColor("#4f5866");
    t.line = QColor("#cfd5dc");
    t.lineStrong = QColor("#0f1319");
    t.edge = QColor("#0f1319");
    t.sign = QColor("#ffd200");
    t.signInk = QColor("#0f1319");
    t.board = QColor("#1b2230");
    t.board2 = QColor("#232c3a");
    t.boardLine = QColor("#2e3848");
    t.boardFg = QColor("#eef1f5");
    t.boardMuted = QColor("#a3aec0");
    t.focus = QColor("#0f1319");
    t.tone2 = QColor("#00a26b");
    t.tone3 = QColor("#2f6bff");
    t.tone3Ink = QColor("#ffffff");
    t.good = QColor("#067a50");
    t.goodSoft = QColor("#d4efe4");
    t.warn = QColor("#8a5a00");
    t.warnSoft = QColor("#fbecc2");
    t.bad = QColor("#b3261e");
    t.badSoft = QColor("#f8dcd8");
    t.inputBg = QColor("#ffffff");
    t.tgOff = QColor("#c9d0d8");
    t.tgOn = t.board;
    t.tgKnob = QColor("#ffffff");
    t.tgKnobOn = t.sign;
    t.dark = false;
    return t;
}

// tokens.css, dark
DeparturesTheme::Tokens darkTokens()
{
    DeparturesTheme::Tokens t = lightTokens();
    t.bg = QColor("#0f141c");
    t.surface = QColor("#171e29");
    t.surface2 = QColor("#232c3a");
    t.text = QColor("#eef1f5");
    t.muted = QColor("#a3aec0");
    t.line = QColor("#2a3342");
    t.lineStrong = QColor("#eef1f5");
    t.edge = QColor("#6c7889");
    t.board = QColor("#06090d");
    t.board2 = QColor("#151c27");
    t.boardLine = QColor("#222b38");
    t.focus = QColor("#ffd200");
    t.tone2 = QColor("#2fcf92");
    t.tone3 = QColor("#6b9bff");
    t.tone3Ink = QColor("#0f1319");
    t.good = QColor("#57d49f");
    t.goodSoft = QColor(87, 212, 159, 33);
    t.warn = QColor("#f2c14e");
    t.warnSoft = QColor(242, 193, 78, 33);
    t.bad = QColor("#ff8a80");
    t.badSoft = QColor(255, 138, 128, 33);
    t.inputBg = QColor("#171e29");
    t.tgOff = QColor("#2e3848");
    t.tgOn = t.sign;
    t.tgKnob = QColor("#a3aec0");
    t.tgKnobOn = QColor("#0f1319");
    t.dark = true;
    return t;
}

QString css(const QColor& c)
{
    if (c.alpha() == 255) {
        return c.name();
    }
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

// color-mix(in srgb, a p%, b)
QColor mix(const QColor& a, const QColor& b, qreal p)
{
    return QColor::fromRgbF(a.redF() * p + b.redF() * (1 - p),
                            a.greenF() * p + b.greenF() * (1 - p),
                            a.blueF() * p + b.blueF() * (1 - p));
}

// Fusion, minus the details that don't fit Departures
class DeparturesStyle : public QProxyStyle {
public:
    DeparturesStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override
    {
        switch (hint) {
            case SH_DialogButtonBox_ButtonsHaveIcons: return 0; // words, not icons
            case SH_UnderlineShortcut: return 0;                // shortcuts still work
            default: return QProxyStyle::styleHint(hint, option, widget, returnData);
        }
    }
};

void loadFonts()
{
    static bool loaded = false;
    if (loaded) {
        return;
    }
    loaded = true;
    const char* files[] = {
        "Barlow-Regular", "Barlow-Medium", "Barlow-SemiBold", "Barlow-Bold",
        "BarlowCondensed-SemiBold", "BarlowCondensed-Bold", "BarlowCondensed-ExtraBold",
    };
    for (const char* file : files) {
        QFontDatabase::addApplicationFont(QString(":/res/fonts/%1.ttf").arg(file));
    }
}

} // namespace

DeparturesTheme::DeparturesTheme(QApplication& app) :
    QObject(&app),
    app_(app)
{
}

DeparturesTheme* DeparturesTheme::install(QApplication& app)
{
    if (s_instance == nullptr) {
        loadFonts();
        s_instance = new DeparturesTheme(app);
        // Fusion draws everything from the palette and stylesheet, so the
        // result is the same on Windows, KDE and macOS
        app.setStyle(new DeparturesStyle);
        app.installEventFilter(s_instance);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                s_instance, [](Qt::ColorScheme) { s_instance->apply(); });
#endif
        s_instance->apply();
    }
    return s_instance;
}

DeparturesTheme* DeparturesTheme::instance()
{
    return s_instance;
}

QFont DeparturesTheme::display(int pixelSize, int weight, qreal letterSpacing)
{
    QFont font(QStringLiteral("Barlow Condensed"));
    font.setPixelSize(pixelSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    if (letterSpacing != 0) {
        font.setLetterSpacing(QFont::PercentageSpacing, 100 + letterSpacing * 100);
    }
    return font;
}

QFont DeparturesTheme::body(int pixelSize, int weight)
{
    QFont font(QStringLiteral("Barlow"));
    font.setPixelSize(pixelSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

namespace {

QPixmap paintedIcon(int width, int height, const std::function<void(QPainter&)>& paint)
{
    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(QSize(width, height) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    paint(painter);
    return pixmap;
}

} // namespace

QPixmap DeparturesTheme::screenIcon()
{
    const Tokens t = s_instance ? s_instance->tokens() : lightTokens();
    return paintedIcon(64, 48, [&](QPainter& p) {
        const QRectF board(4, 4, 56, 40);
        p.fillRect(board, t.dark ? t.surface2 : t.board);
        p.fillRect(QRectF(board.left(), board.top(), board.width(), 9), t.sign);
        // rows on the board
        p.setPen(QPen(t.boardMuted, 2, Qt::SolidLine, Qt::SquareCap));
        p.drawLine(QPointF(12, 22), QPointF(44, 22));
        p.drawLine(QPointF(12, 30), QPointF(36, 30));
        p.setPen(QPen(t.sign, 2, Qt::SolidLine, Qt::SquareCap));
        p.drawLine(QPointF(12, 38), QPointF(24, 38));
    });
}

QPixmap DeparturesTheme::trashIcon()
{
    const Tokens t = s_instance ? s_instance->tokens() : lightTokens();
    return paintedIcon(48, 48, [&](QPainter& p) {
        p.setPen(QPen(t.text, 3, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(10, 13), QPointF(38, 13));          // lid
        p.drawLine(QPointF(19, 8), QPointF(29, 8));            // handle
        p.drawRect(QRectF(14, 17, 20, 23));                    // bin
        p.drawLine(QPointF(21, 23), QPointF(21, 34));
        p.drawLine(QPointF(27, 23), QPointF(27, 34));
    });
}

bool DeparturesTheme::reducedMotion()
{
    return qEnvironmentVariableIntValue("INPUTGATE_REDUCED_MOTION") != 0;
}

bool DeparturesTheme::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Polish || !watched->isWidgetType()) {
        return QObject::eventFilter(watched, event);
    }
    auto* widget = static_cast<QWidget*>(watched);

    if (auto* group = qobject_cast<QGroupBox*>(widget)) {
        // section titles are condensed capitals; the font comes with the title
        QFont font = display(17, QFont::Bold, 0.01);
        font.setCapitalization(QFont::AllUppercase);
        group->setFont(font);
    } else if (!widget->testAttribute(Qt::WA_SetFont)) {
        // ...but what is inside a section keeps the reading font
        for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
            if (qobject_cast<QGroupBox*>(parent)) {
                widget->setFont(QApplication::font());
                break;
            }
        }
    }

    if (auto* box = qobject_cast<QDialogButtonBox*>(widget)) {
        // the button that confirms is the main action: sign yellow
        for (QAbstractButton* button : box->buttons()) {
            auto role = box->buttonRole(button);
            if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole) {
                button->setProperty("primary", true);
                button->style()->unpolish(button);
                button->style()->polish(button);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

bool DeparturesTheme::wantDark() const
{
    const QByteArray forced = qgetenv("INPUTGATE_THEME");
    if (forced == "dark") {
        return true;
    }
    if (forced == "light") {
        return false;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown) {
        return scheme == Qt::ColorScheme::Dark;
    }
#endif
    // Before Qt 6.5: look at the platform palette we started with
    return QApplication::style()->standardPalette().color(QPalette::Window).lightness() < 128;
}

void DeparturesTheme::apply()
{
    if (applying_) {
        return;
    }
    applying_ = true;

    tokens_ = wantDark() ? darkTokens() : lightTokens();
    const Tokens& t = tokens_;

    QPalette p;
    p.setColor(QPalette::Window, t.bg);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.inputBg);
    p.setColor(QPalette::AlternateBase, t.surface2);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::PlaceholderText, t.muted);
    p.setColor(QPalette::Button, t.board);
    p.setColor(QPalette::ButtonText, QColor("#ffffff"));
    p.setColor(QPalette::Highlight, t.sign);
    p.setColor(QPalette::HighlightedText, t.signInk);
    p.setColor(QPalette::ToolTipBase, t.board);
    p.setColor(QPalette::ToolTipText, t.boardFg);
    p.setColor(QPalette::Link, t.dark ? t.sign : t.tone3);
    p.setColor(QPalette::Light, t.surface);
    p.setColor(QPalette::Midlight, t.surface2);
    p.setColor(QPalette::Mid, t.line);
    p.setColor(QPalette::Dark, t.edge);
    p.setColor(QPalette::Shadow, t.board);
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText }) {
        p.setColor(QPalette::Disabled, role, t.muted);
    }
    app_.setPalette(p);

    QFont font = body(14);
    app_.setFont(font);

    iconDir_ = writeIcons();
    app_.setStyleSheet(styleSheet());

    applying_ = false;
    emit changed();
}

QString DeparturesTheme::writeIcons() const
{
    // Stylesheet images can't use currentColor, so draw them per theme. Drawn
    // with QPainter at 2x (not SVG) so no image format plugin is needed.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                  QStringLiteral("/inputgate-theme/") + (tokens_.dark ? "dark" : "light");
    QDir().mkpath(dir);
    const Tokens& t = tokens_;

    auto draw = [&](const QString& name, int size, const std::function<void(QPainter&)>& paint) {
        QImage image(size * 2, size * 2, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(2, 2);
        paint(painter);
        painter.end();
        image.save(dir + "/" + name + ".png");
    };
    auto stroke = [](const QColor& color, qreal width) {
        return QPen(color, width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    };

    draw("check", 16, [&](QPainter& p) {
        p.setPen(stroke(t.signInk, 2.4));
        p.drawPolyline(QPolygonF({ QPointF(3.5, 8.5), QPointF(6.5, 11.3), QPointF(12.5, 4.8) }));
    });
    draw("dot", 16, [&](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.signInk);
        p.drawEllipse(QPointF(8, 8), 3.4, 3.4);
    });
    draw("down", 12, [&](QPainter& p) {
        p.setPen(stroke(t.text, 2));
        p.drawPolyline(QPolygonF({ QPointF(2.5, 4.5), QPointF(6, 8), QPointF(9.5, 4.5) }));
    });
    draw("up", 12, [&](QPainter& p) {
        p.setPen(stroke(t.text, 2));
        p.drawPolyline(QPolygonF({ QPointF(2.5, 7.5), QPointF(6, 4), QPointF(9.5, 7.5) }));
    });
    return dir;
}

QString DeparturesTheme::styleSheet() const
{
    const Tokens& t = tokens_;
    const QColor signHover = mix(t.sign, QColor("#000000"), 0.88);
    // In dark mode the default button is a raised surface instead of the board
    const QColor btnBg = t.dark ? t.surface2 : t.board;
    const QColor btnHover = t.dark ? mix(t.surface2, QColor("#ffffff"), 0.9) : t.board2;
    const QColor focusBg = t.dark ? t.inputBg : mix(t.sign, t.inputBg, 0.18);
    const QString icons = iconDir_;

    QString qss = R"QSS(
* { outline: none; }
QWidget { color: @text; }
QMainWindow, QDialog, QWizard, QWizardPage, QMessageBox { background: @bg; }
QLabel { background: transparent; }
QLabel:disabled { color: @muted; }

/* sections: square white panels with a condensed label above */
QGroupBox {
    background: @surface; border: none; border-top: 2px solid @lineStrong;
    margin-top: 30px; padding: 14px 16px 16px 16px;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left; left: 0px; top: 2px;
    padding: 0px; color: @text;
    font-family: "Barlow Condensed"; font-size: 17px; font-weight: 700;
}
QGroupBox:disabled { border-top-color: @line; }

/* buttons: square board-coloured signs, the main action in sign yellow */
QPushButton {
    background: @btnBg; color: #ffffff; border: 2px solid @btnBg; border-radius: 0px;
    min-height: 32px; padding: 0px 16px;
    font-family: "Barlow Condensed"; font-size: 17px; font-weight: 700;
}
QPushButton:hover { background: @btnHover; border-color: @btnHover; }
QPushButton:pressed { background: @board; border-color: @board; }
QPushButton:focus { border-color: @sign; }
QPushButton:disabled { background: transparent; color: @muted; border-color: @line; }
QPushButton[primary="true"] { background: @sign; color: @signInk; border-color: @sign; }
QPushButton[primary="true"]:hover { background: @signHover; border-color: @signHover; }
QPushButton[primary="true"]:focus { border-color: @text; }
QPushButton[primary="true"]:disabled { background: @surface2; color: @muted; border-color: @surface2; }
QPushButton[go="true"] { padding-right: 44px; }
QPushButton[quiet="true"] { background: transparent; color: @text; border-color: transparent; }
QPushButton[quiet="true"]:hover { background: @surface2; }
QDialogButtonBox QPushButton { min-width: 88px; }

QToolButton {
    background: transparent; color: @text; border: 2px solid transparent; border-radius: 0px;
    padding: 2px 6px; font-family: "Barlow Condensed"; font-size: 15px; font-weight: 700;
}
QToolButton:hover { background: @surface2; }
QToolButton:focus { border-color: @sign; }

/* fields */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QKeySequenceEdit {
    background: @inputBg; color: @text; border: 2px solid @edge; border-radius: 0px;
    min-height: 30px; padding: 0px 10px;
    selection-background-color: @sign; selection-color: @signInk;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: @focusBorder; background: @focusBg;
}
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    color: @muted; border-color: @line; background: transparent;
}
QPlainTextEdit, QTextEdit, QListView, QListWidget, QTreeView, QTableView {
    background: @inputBg; color: @text; border: 2px solid @edge; border-radius: 0px;
    selection-background-color: @sign; selection-color: @signInk;
}
QComboBox { padding-right: 30px; }
QComboBox::drop-down { width: 28px; border: none; subcontrol-position: center right; }
QComboBox::down-arrow { image: url(@icons/down.png); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background: @surface; color: @text; border: 2px solid @edge; padding: 0px;
    selection-background-color: @sign; selection-color: @signInk;
}
QSpinBox, QDoubleSpinBox { padding-right: 24px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 22px; border: none; background: transparent;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(@icons/up.png); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(@icons/down.png); width: 10px; height: 10px; }

/* checkbox, radio: square box, round dot, yellow when on */
QCheckBox, QRadioButton { spacing: 10px; background: transparent; }
QCheckBox::indicator, QRadioButton::indicator, QGroupBox::indicator {
    width: 16px; height: 16px; border: 2px solid @edge; background: @inputBg; border-radius: 0px;
}
QRadioButton::indicator { border-radius: 10px; }
QCheckBox::indicator:checked, QGroupBox::indicator:checked {
    background: @sign; border-color: @edgeOn; image: url(@icons/check.png);
}
QRadioButton::indicator:checked { background: @sign; border-color: @edgeOn; image: url(@icons/dot.png); }
QCheckBox::indicator:focus, QRadioButton::indicator:focus { border-color: @focusBorder; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: @line; background: transparent; }

/* tabs: plain words with a thick yellow bar under the current one */
QTabWidget::pane { border: none; border-top: 2px solid @lineStrong; top: -2px; background: transparent; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: @muted; border: none; border-bottom: 4px solid transparent;
    padding: 8px 0px 8px 0px; margin-right: 22px;
    font-family: "Barlow Condensed"; font-size: 17px; font-weight: 700;
}
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { color: @text; border-bottom-color: @sign; }

/* menus and tooltips are small departure boards */
/* the menu bar is part of the yellow sign */
QMenuBar { background: @sign; color: @signInk; padding: 2px 14px 0px 14px;
    font-family: "Barlow Condensed"; font-size: 15px; font-weight: 700; }
QMenuBar::item { background: transparent; color: @signInk; padding: 5px 10px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @signInk; color: @sign; }
QMenu { background: @board; color: @boardFg; border: none; padding: 6px 0px; }
QMenu::item { padding: 7px 24px 7px 16px; background: transparent; }
QMenu::item:selected { background: @sign; color: @signInk; }
QMenu::item:disabled { color: @boardMuted; }
QMenu::separator { height: 1px; background: @boardLine; margin: 6px 0px; }
QToolTip { background: @board; color: @boardFg; border: none; padding: 6px 8px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0px; }
QScrollBar::handle { background: @line; border: none; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; border: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QProgressBar { background: @surface2; border: none; height: 4px; text-align: center; color: transparent; }
QProgressBar::chunk { background: @sign; }

QSlider::groove:horizontal { height: 4px; background: @surface2; }
QSlider::sub-page:horizontal { background: @sign; }
QSlider::handle:horizontal { width: 14px; margin: -6px 0px; background: @text; }

/* dark departure boards: the log, the status board */
*[board="true"] { background: @board; color: @boardFg; border: none; }
QPlainTextEdit[board="true"], QTextEdit[board="true"] {
    font-family: "Cascadia Mono", "JetBrains Mono", "Consolas", "DejaVu Sans Mono", monospace;
    font-size: 12px; selection-background-color: @sign; selection-color: @signInk;
}
QLabel[muted="true"] { color: @muted; }
QLabel[caption="true"] {
    color: @muted; font-family: "Barlow Condensed"; font-size: 13px; font-weight: 700;
}
)QSS";

    const std::pair<const char*, QString> vars[] = {
        { "@btnHover", css(btnHover) },
        { "@btnBg", css(btnBg) },
        { "@signHover", css(signHover) },
        { "@signInk", css(t.signInk) },
        { "@sign", css(t.sign) },
        { "@focusBorder", css(t.dark ? t.sign : t.text) },
        { "@focusBg", css(focusBg) },
        { "@bg", css(t.bg) },
        { "@surface2", css(t.surface2) },
        { "@surface", css(t.surface) },
        { "@text", css(t.text) },
        { "@muted", css(t.muted) },
        { "@lineStrong", css(t.lineStrong) },
        { "@line", css(t.line) },
        { "@edgeOn", css(t.dark ? t.text : t.edge) },
        { "@edge", css(t.edge) },
        { "@boardLine", css(t.boardLine) },
        { "@boardMuted", css(t.boardMuted) },
        { "@boardFg", css(t.boardFg) },
        { "@board", css(t.board) },
        { "@inputBg", css(t.inputBg) },
        { "@icons", icons },
    };
    for (const auto& var : vars) {
        qss.replace(QString::fromLatin1(var.first), var.second);
    }
    return qss;
}
