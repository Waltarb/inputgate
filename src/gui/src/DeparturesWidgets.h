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

#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QPushButton>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class QLabel;

/*! Text that flips into place like a split-flap departure board: each
    character spins through the board alphabet, then lands. Only for board
    text, titles and numbers, and only when they change. */
class SplitFlapLabel : public QWidget {
    Q_OBJECT
public:
    explicit SplitFlapLabel(QWidget* parent = nullptr);

    void setFlapFont(const QFont& font);
    void setColor(const QColor& color);
    //! Show \p text (upper-cased); flips if \p animate and the text changed
    void setText(const QString& text, bool animate = true);
    QString text() const { return target_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal cellWidth(QChar c) const;

    QString target_;
    QString previous_;
    QColor color_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool animating_ = false;
    int perChar_ = 24;
    int spins_ = 5;
};

//! The round gate code in a route colour, which pops when it changes
class GateCode : public QWidget {
    Q_OBJECT
public:
    explicit GateCode(QWidget* parent = nullptr);
    void setGate(const QString& code, const QColor& fill, const QColor& ink);
    QSize sizeHint() const override { return QSize(42, 42); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString code_;
    QColor fill_, ink_;
    qreal scale_ = 1.0;
    QVariantAnimation anim_;
};

//! The yellow sign bar at the top of the window: where you are.
class SignHeader : public QWidget {
    Q_OBJECT
public:
    explicit SignHeader(QWidget* parent = nullptr);

    //! Right hand side of the sign, e.g. the screen name
    void setPlace(const QString& place);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString place_;
};

/*! The departure board: a dark panel with a caption and one row showing
    what Inputgate is doing, with a round gate code, a split-flap line,
    details and a status word. */
class StatusBoard : public QFrame {
    Q_OBJECT
public:
    enum class Badge { Neutral, Good, Busy, Bad };

    explicit StatusBoard(QWidget* parent = nullptr);

    //! Gate code (a character or two) and route colour (1 = sign, 2 = green, 3 = blue)
    void setGate(const QString& code, int tone);
    void setStatus(const QString& line, const QString& meta, const QString& badge, Badge kind);
    //! Change only the flipping line, e.g. when a screen connects
    void setLine(const QString& line);

private:
    void updateColors();
    void updateClock();

    QLabel* caption_;
    QLabel* clock_;
    QFrame* rule_;
    GateCode* gate_;
    SplitFlapLabel* line_;
    QLabel* meta_;
    SplitFlapLabel* badge_;
    QString code_ = QStringLiteral("S");
    int tone_ = 1;
    Badge kind_ = Badge::Neutral;
    QTimer clockTimer_;
};

/*! A button with a direction: an arrow after the label that pushes forward
    on hover and shoots off on click. Use for the main action. */
class GoButton : public QPushButton {
    Q_OBJECT
public:
    explicit GoButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent*) override;
#else
    void enterEvent(QEvent*) override;
#endif
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    qreal offset_ = 0;
    qreal opacity_ = 1;
    QVariantAnimation anim_;
};
