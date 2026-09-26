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

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPixmap>
#include <QString>

class QApplication;

/*! The Departures design system (Schiphol style wayfinding) as a Qt theme.

    Colours mirror tokens.css of WaltaDesign/designs/departures. Components
    use only these values. Light and dark follow the OS; INPUTGATE_THEME=light
    or dark forces one.
*/
class DeparturesTheme : public QObject {
    Q_OBJECT
public:
    struct Tokens {
        QColor bg, surface, surface2, text, muted, line, lineStrong, edge;
        QColor sign, signInk, board, board2, boardLine, boardFg, boardMuted;
        QColor focus, tone2, tone3, tone3Ink;
        QColor good, goodSoft, warn, warnSoft, bad, badSoft;
        QColor inputBg, tgOff, tgOn, tgKnob, tgKnobOn;
        bool dark = false;
    };

    //! Installs fonts, palette and stylesheet on the application
    static DeparturesTheme* install(QApplication& app);
    static DeparturesTheme* instance();

    const Tokens& tokens() const { return tokens_; }

    //! Barlow Condensed at the given pixel size and weight, for signs and boards
    static QFont display(int pixelSize, int weight = QFont::Bold, qreal letterSpacing = 0);
    //! Barlow at the given pixel size and weight, for reading
    static QFont body(int pixelSize, int weight = QFont::Normal);

    //! A screen: a small departure board with a yellow sign on top
    static QPixmap screenIcon();
    //! Drag here to remove: a square line-art bin
    static QPixmap trashIcon();

    //! Whether motion should be skipped (INPUTGATE_REDUCED_MOTION=1)
    static bool reducedMotion();

signals:
    void changed();

private:
    explicit DeparturesTheme(QApplication& app);
    void apply();
    bool wantDark() const;
    QString styleSheet() const;
    QString writeIcons() const;
    bool eventFilter(QObject* watched, QEvent* event) override;

    QApplication& app_;
    Tokens tokens_;
    QString iconDir_;
    bool applying_ = false;
};
