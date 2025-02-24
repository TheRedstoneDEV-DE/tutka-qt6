/*
 * instrumentpropertiesdialog.h
 *
 * Copyright 2002-2019 Vesa Halttunen
 *
 * This file is part of Tutka.
 *
 * Tutka is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Tutka is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Tutka; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef INSTRUMENTPROPERTIESDIALOG_H
#define INSTRUMENTPROPERTIESDIALOG_H

#include "tutkadialog.h"

class MIDI;
class Song;

namespace Ui {
    class InstrumentPropertiesDialog;
}

class InstrumentPropertiesDialog : public TutkaDialog
{
    Q_OBJECT

public:
    explicit InstrumentPropertiesDialog(MIDI *midi, QWidget *parent = NULL);
    ~InstrumentPropertiesDialog();

public slots:
    void makeVisible();
    void setSong(Song *song);
    void setInstrument(int number);

private slots:
    void updateMidiInterfaceComboBox();
    void updateArpeggio();
    void setMidiInterface(const int index);
    void setMidiInterfaceName(const int index);
    void setMidiChannel(int midiChannel);
    void toggleArpeggio(bool enabled);
    void setArpeggioBaseNote(int baseNote);
    void setArpeggioLength(int length);
    void advanceTrackerToNextLine();

private:
    MIDI *midi;
    Ui::InstrumentPropertiesDialog *ui;
    Song *song;
    int instrument;
};

#endif // INSTRUMENTPROPERTIESDIALOG_H
