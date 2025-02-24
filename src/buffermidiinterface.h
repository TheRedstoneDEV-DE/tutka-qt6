/*
 * buffermidiinterface.h
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

#ifndef BUFFERMIDIINTERFACE_H
#define BUFFERMIDIINTERFACE_H

#include "midiinterface.h"

class BufferMIDI;

class BufferMIDIInterface : public MIDIInterface
{
    Q_OBJECT

public:
    explicit BufferMIDIInterface(BufferMIDI *midi, DirectionFlags flags, QObject *parent = NULL);
    QByteArray data() const;
    virtual void tempo(unsigned int);

protected:
    virtual void write(const QByteArray &data);

private:
    BufferMIDI *midi;
    QByteArray data_;
    unsigned int oldTick;
};

#endif // BUFFERMIDIINTERFACE_H
