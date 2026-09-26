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

#include "DeparturesWidgets.h"
#include "DeparturesTheme.h"

#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const QString kFlapAlphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
const int kSpinStepMs = 42;
const int kLandMs = 120;

DeparturesTheme::Tokens tokens()
{
    auto* theme = DeparturesTheme::instance();
    return theme ? theme->tokens() : DeparturesTheme::Tokens{};
}

QString colorRule(const QColor& color)
{
    return QStringLiteral("color: %1; background: transparent;").arg(color.name());
}

} // namespace

// ---------------------------------------------------------------- SplitFlapLabel

SplitFlapLabel::SplitFlapLabel(QWidget* parent) :
    QWidget(parent),
    color_(Qt::white)
{
    setFlapFont(DeparturesTheme::display(21, QFont::Bold));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, [this]() {
        const qint64 total = static_cast<qint64>(target_.size()) * perChar_ + spins_ * kSpinStepMs;
        if (clock_.elapsed() >= total) {
            animating_ = false;
            timer_.stop();
        }
        update();
    });
}

void SplitFlapLabel::setFlapFont(const QFont& font)
{
    setFont(font);
    updateGeometry();
    update();
}

void SplitFlapLabel::setColor(const QColor& color)
{
    color_ = color;
    update();
}

void SplitFlapLabel::setText(const QString& text, bool animate)
{
    const QString upper = text.toUpper();
    if (upper == target_) {
        return;
    }
    previous_ = target_;
    target_ = upper;
    setAccessibleName(text);
    updateGeometry();

    if (animate && !DeparturesTheme::reducedMotion() && isVisible()) {
        animating_ = true;
        clock_.start();
        timer_.start();
    } else {
        animating_ = false;
        timer_.stop();
    }
    update();
}

qreal SplitFlapLabel::cellWidth(QChar c) const
{
    // Board characters sit in fixed cells, like the flaps on a real board
    const QFontMetricsF metrics(font());
    const qreal cell = metrics.horizontalAdvance(QLatin1Char('0')) * 1.02;
    if (c.isSpace()) {
        return cell * 0.55;
    }
    return std::max(cell, metrics.horizontalAdvance(c));
}

QSize SplitFlapLabel::sizeHint() const
{
    qreal width = 0;
    for (QChar c : target_) {
        width += cellWidth(c);
    }
    const QFontMetrics metrics(font());
    return QSize(static_cast<int>(std::ceil(width)) + 4, metrics.height());
}

QSize SplitFlapLabel::minimumSizeHint() const
{
    return QSize(0, QFontMetrics(font()).height());
}

void SplitFlapLabel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font());
    painter.setPen(color_);

    const QFontMetricsF metrics(font());
    const qreal baseline = (height() - metrics.height()) / 2 + metrics.ascent();
    const qreal centerY = baseline - metrics.ascent() / 2 + metrics.descent() / 2;
    const qint64 now = animating_ ? clock_.elapsed() : std::numeric_limits<qint64>::max() / 2;
    const qint64 spinTotal = static_cast<qint64>(spins_) * kSpinStepMs;

    qreal x = 0;
    for (int i = 0; i < target_.size(); ++i) {
        const QChar final = target_.at(i);
        const qreal cell = cellWidth(final);
        if (x + cell > width() + 2) {
            break; // doesn't fit; the board is simply cut off
        }

        QChar shown = final;
        qreal scaleY = 1.0;
        const qint64 start = static_cast<qint64>(i) * perChar_;
        if (!final.isSpace() && now < start + spinTotal) {
            if (now < start) {
                // not flipping yet: the old flap is still showing
                shown = i < previous_.size() ? previous_.at(i) : QChar(' ');
            } else {
                const qint64 step = (now - start) / kSpinStepMs;
                const uint pick = qHash(qMakePair(i, static_cast<int>(step))) % kFlapAlphabet.size();
                shown = kFlapAlphabet.at(static_cast<int>(pick));
                const qint64 untilLand = start + spinTotal - now;
                if (untilLand < kLandMs) {
                    const qreal p = 1.0 - static_cast<qreal>(untilLand) / kLandMs;
                    scaleY = 0.2 + 0.8 * QEasingCurve(QEasingCurve::OutQuad).valueForProgress(p);
                    shown = final;
                }
            }
        }

        if (!shown.isSpace()) {
            painter.save();
            painter.translate(x + cell / 2, centerY);
            painter.scale(1.0, scaleY);
            const qreal glyph = metrics.horizontalAdvance(shown);
            painter.drawText(QPointF(-glyph / 2, baseline - centerY), QString(shown));
            painter.restore();
        }
        x += cell;
    }
}

// ---------------------------------------------------------------- GateCode

GateCode::GateCode(QWidget* parent) :
    QWidget(parent)
{
    setFixedSize(42, 42);
    anim_.setDuration(420);
    anim_.setEasingCurve(QEasingCurve::OutBack);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        scale_ = v.toReal();
        update();
    });
}

void GateCode::setGate(const QString& code, const QColor& fill, const QColor& ink)
{
    const bool changed = code != code_;
    code_ = code;
    fill_ = fill;
    ink_ = ink;
    setAccessibleName(code);
    if (changed && !DeparturesTheme::reducedMotion() && isVisible()) {
        anim_.stop();
        anim_.setStartValue(0.0);
        anim_.setEndValue(1.0);
        anim_.start();
    }
    update();
}

void GateCode::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(scale_, scale_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill_);
    painter.drawEllipse(QPointF(0, 0), 21, 21);
    painter.setPen(ink_);
    painter.setFont(DeparturesTheme::display(19, QFont::ExtraBold));
    painter.drawText(QRectF(-21, -21, 42, 42), Qt::AlignCenter, code_);
}

// ---------------------------------------------------------------- SignHeader

SignHeader::SignHeader(QWidget* parent) :
    QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QStringLiteral("Inputgate"));
    if (auto* theme = DeparturesTheme::instance()) {
        connect(theme, &DeparturesTheme::changed, this, qOverload<>(&QWidget::update));
    }
}

void SignHeader::setPlace(const QString& place)
{
    place_ = place.toUpper();
    update();
}

QSize SignHeader::sizeHint() const
{
    return QSize(420, 64);
}

void SignHeader::paintEvent(QPaintEvent*)
{
    const auto t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), t.sign);

    // logo: an arrow going through a gate, in a black square
    const QRectF mark(24, (height() - 30) / 2.0, 30, 30);
    painter.fillRect(mark, t.signInk);
    QPen pen(t.sign, 2.4, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(pen);
    const qreal cx = mark.left(), cy = mark.center().y();
    painter.drawLine(QPointF(cx + 9, cy - 7), QPointF(cx + 9, cy + 7));   // gate post
    painter.drawLine(QPointF(cx + 12, cy), QPointF(cx + 22, cy));         // arrow shaft
    painter.drawLine(QPointF(cx + 18, cy - 4), QPointF(cx + 22, cy));     // arrow head
    painter.drawLine(QPointF(cx + 18, cy + 4), QPointF(cx + 22, cy));

    painter.setPen(t.signInk);
    painter.setFont(DeparturesTheme::display(26, QFont::ExtraBold, 0.01));
    const QRectF titleRect(mark.right() + 10, 0, width() / 2.0, height());
    painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("INPUTGATE"));

    if (!place_.isEmpty()) {
        QColor muted = t.signInk;
        muted.setAlphaF(0.7);
        painter.setPen(muted);
        painter.setFont(DeparturesTheme::display(17, QFont::Bold, 0.04));
        painter.drawText(QRectF(0, 0, width() - 24, height()), Qt::AlignVCenter | Qt::AlignRight,
                         place_);
    }
}

// ---------------------------------------------------------------- StatusBoard

StatusBoard::StatusBoard(QWidget* parent) :
    QFrame(parent)
{
    setProperty("board", true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* captionRow = new QHBoxLayout;
    captionRow->setContentsMargins(20, 12, 20, 11);
    caption_ = new QLabel(tr("STATUS"), this);
    clock_ = new QLabel(this);
    for (QLabel* label : { caption_, clock_ }) {
        label->setFont(DeparturesTheme::display(12, QFont::Bold, 0.1));
    }
    captionRow->addWidget(caption_);
    captionRow->addStretch();
    captionRow->addWidget(clock_);
    layout->addLayout(captionRow);

    rule_ = new QFrame(this);
    rule_->setFixedHeight(1);
    layout->addWidget(rule_);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(20, 16, 20, 16);
    row->setSpacing(14);
    gate_ = new GateCode(this);
    row->addWidget(gate_, 0, Qt::AlignVCenter);

    auto* main = new QVBoxLayout;
    main->setSpacing(2);
    line_ = new SplitFlapLabel(this);
    line_->setFlapFont(DeparturesTheme::display(21, QFont::Bold, 0.01));
    meta_ = new QLabel(this);
    meta_->setFont(DeparturesTheme::display(15, QFont::DemiBold));
    meta_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    main->addWidget(line_);
    main->addWidget(meta_);
    row->addLayout(main, 1);

    badge_ = new SplitFlapLabel(this);
    badge_->setFlapFont(DeparturesTheme::display(15, QFont::Bold, 0.04));
    badge_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    row->addWidget(badge_, 0, Qt::AlignVCenter);
    layout->addLayout(row);

    updateClock();
    clockTimer_.setInterval(5000);
    connect(&clockTimer_, &QTimer::timeout, this, &StatusBoard::updateClock);
    clockTimer_.start();

    if (auto* theme = DeparturesTheme::instance()) {
        connect(theme, &DeparturesTheme::changed, this, &StatusBoard::updateColors);
    }
    updateColors();
}

void StatusBoard::setGate(const QString& code, int tone)
{
    code_ = code;
    tone_ = tone;
    updateColors();
}

void StatusBoard::setStatus(const QString& line, const QString& meta, const QString& badge, Badge kind)
{
    kind_ = kind;
    line_->setText(line);
    meta_->setText(meta);
    badge_->setText(badge);
    badge_->setFixedWidth(badge_->sizeHint().width());
    updateColors();
}

void StatusBoard::setLine(const QString& line)
{
    line_->setText(line);
}

void StatusBoard::updateClock()
{
    const QString now = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    if (clock_->text() != now) {
        clock_->setText(now);
    }
}

void StatusBoard::updateColors()
{
    const auto t = tokens();
    caption_->setStyleSheet(colorRule(t.sign));
    clock_->setStyleSheet(colorRule(t.sign));
    rule_->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(t.boardLine.name()));
    meta_->setStyleSheet(colorRule(t.boardMuted));
    line_->setColor(t.boardFg);

    switch (kind_) {
        case Badge::Good: badge_->setColor(t.sign); break;
        case Badge::Bad: badge_->setColor(QColor("#ff9d94")); break;
        case Badge::Busy: badge_->setColor(t.boardFg); break;
        default: badge_->setColor(t.boardMuted); break;
    }

    switch (tone_) {
        case 2: gate_->setGate(code_, t.tone2, QColor("#0f1319")); break;
        case 3: gate_->setGate(code_, t.tone3, t.tone3Ink); break;
        default: gate_->setGate(code_, t.sign, t.signInk); break;
    }
}

// ---------------------------------------------------------------- GoButton

GoButton::GoButton(QWidget* parent) :
    QPushButton(parent)
{
    setProperty("go", true);
    setCursor(Qt::PointingHandCursor);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        offset_ = v.toReal();
        update();
    });
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void GoButton::enterEvent(QEnterEvent* event)
#else
void GoButton::enterEvent(QEvent* event)
#endif
{
    QPushButton::enterEvent(event);
    if (DeparturesTheme::reducedMotion() || !isEnabled()) {
        return;
    }
    // push forward, with a little spring
    anim_.stop();
    anim_.setKeyValues({});
    anim_.setDuration(260);
    anim_.setEasingCurve(QEasingCurve(QEasingCurve::OutBack));
    anim_.setStartValue(offset_);
    anim_.setEndValue(5.0);
    anim_.start();
}

void GoButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    if (DeparturesTheme::reducedMotion()) {
        return;
    }
    anim_.stop();
    anim_.setKeyValues({});
    anim_.setDuration(200);
    anim_.setEasingCurve(QEasingCurve::OutCubic);
    anim_.setStartValue(offset_);
    anim_.setEndValue(0.0);
    anim_.start();
}

void GoButton::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasDown = isDown();
    QPushButton::mouseReleaseEvent(event);
    if (!wasDown || DeparturesTheme::reducedMotion()) {
        return;
    }
    // shoot off to the right, come back in from the left
    anim_.stop();
    anim_.setDuration(460);
    anim_.setEasingCurve(QEasingCurve::Linear);
    anim_.setKeyValues({
        { 0.0, offset_ },
        { 0.39, 40.0 },
        { 0.3901, -40.0 },
        { 0.6, -12.0 },
        { 1.0, underMouse() ? 5.0 : 0.0 },
    });
    anim_.start();
}

void GoButton::paintEvent(QPaintEvent* event)
{
    QPushButton::paintEvent(event);

    const auto t = tokens();
    QColor ink = property("primary").toBool() ? t.signInk : QColor(Qt::white);
    if (!isEnabled()) {
        ink = t.muted;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(rect().adjusted(2, 2, -2, -2));
    QPen pen(ink, 2.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(pen);

    const qreal size = 16;
    const qreal x = width() - 16 - size + offset_;
    const qreal y = height() / 2.0;
    painter.drawLine(QPointF(x + 1, y), QPointF(x + size - 1, y));
    painter.drawLine(QPointF(x + size - 6, y - 5), QPointF(x + size - 1, y));
    painter.drawLine(QPointF(x + size - 6, y + 5), QPointF(x + size - 1, y));
}
