/*
 * instrument.cpp
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

#include <QDomElement>
#include "block.h"
#include "instrument.h"

Instrument::Instrument(const QString &name, unsigned int midiInterface, QObject *parent) :
    QObject(parent),
    name_(name),
    midiInterface_(midiInterface),
    midiInterfaceName_(tr("No output")),
    midiPreset_(0),
    midiChannel_(0),
    defaultVelocity_(127),
    transpose_(0),
    hold_(0),
    arpeggio_(NULL),
    arpeggioBaseNote_(49)
{
}

Instrument::~Instrument()
{
}

QString Instrument::name() const
{
    return name_;
}

void Instrument::setName(const QString &name)
{
    if (name_ != name) {
        name_ = name;

        emit nameChanged(name_);
    }
}

unsigned int Instrument::midiInterface() const
{
    return midiInterface_;
}

void Instrument::setMidiInterface(int interface)
{
    midiInterface_ = interface;
}

QString Instrument::midiInterfaceName() const
{
    return midiInterfaceName_;
}

void Instrument::setMidiInterfaceName(const QString &midiInterfaceName)
{
    midiInterfaceName_ = midiInterfaceName;
}

unsigned short Instrument::midiPreset() const
{
    return midiPreset_;
}

unsigned char Instrument::midiChannel() const
{
    return midiChannel_;
}

void Instrument::setMidiChannel(int midiChannel)
{
    midiChannel_ = midiChannel;
}

unsigned char Instrument::defaultVelocity() const
{
    return defaultVelocity_;
}

void Instrument::setDefaultVelocity(int defaultVelocity)
{
    if (defaultVelocity_ != defaultVelocity) {
        defaultVelocity_ = defaultVelocity;

        emit defaultVelocityChanged(defaultVelocity_);
    }
}

char Instrument::transpose() const
{
    return transpose_;
}

void Instrument::setTranspose(int transpose)
{
    transpose_ = transpose;
}

unsigned char Instrument::hold() const
{
    return hold_;
}

void Instrument::setHold(int hold)
{
    hold_ = hold;
}

Block *Instrument::arpeggio() const
{
    return arpeggio_;
}

void Instrument::setArpeggio(Block *arpeggio)
{
    arpeggio_ = arpeggio;
}

unsigned char Instrument::arpeggioBaseNote() const
{
    return arpeggioBaseNote_;
}

void Instrument::setArpeggioBaseNote(int baseNote)
{
    arpeggioBaseNote_ = baseNote;
}

Instrument *Instrument::parse(QDomElement element)
{
    Instrument *instrument = NULL;

    if (element.tagName() == "instrument") {
        QDomAttr prop;

        // Allocate instrument
        instrument = new Instrument;
        prop = element.attributeNode("name");
        if (!prop.isNull()) {
            instrument->name_ = prop.value();
        } else {
            instrument->name_ = element.text();
        }

        prop = element.attributeNode("midiinterface");
        if (!prop.isNull()) {
            instrument->midiInterfaceName_ = prop.value();
        }

        prop = element.attributeNode("midipreset");
        if (!prop.isNull()) {
            instrument->midiPreset_ = prop.value().toInt();
        }

        prop = element.attributeNode("midichannel");
        if (!prop.isNull()) {
            instrument->midiChannel_ = prop.value().toInt();
        }

        prop = element.attributeNode("defaultvelocity");
        if (!prop.isNull()) {
            instrument->defaultVelocity_ = prop.value().toInt();
        }

        prop = element.attributeNode("transpose");
        if (!prop.isNull()) {
            instrument->transpose_ = prop.value().toInt();
        }

        prop = element.attributeNode("hold");
        if (!prop.isNull()) {
            instrument->hold_ = prop.value().toInt();
        }

        // Get instrument contents
        QDomElement cur = element.firstChild().toElement();
        while(!cur.isNull()) {
            if (cur.tagName() == "output") {
                // Get output properties (tutka 0.12.x)
                prop = cur.attributeNode("midiinterface");
                if (!prop.isNull()) {
                    instrument->midiInterfaceName_ = prop.value();
                }

                prop = cur.attributeNode("midipreset");
                if (!prop.isNull()) {
                    instrument->midiPreset_ = prop.value().toInt();
                }

                prop = cur.attributeNode("midichannel");
                if (!prop.isNull()) {
                    instrument->midiChannel_ = prop.value().toInt();
                }

                prop = cur.attributeNode("defaultvelocity");
                if (!prop.isNull()) {
                    instrument->defaultVelocity_ = prop.value().toInt();
                }

                prop = cur.attributeNode("transpose");
                if (!prop.isNull()) {
                    instrument->transpose_ = prop.value().toInt();
                }

                prop = cur.attributeNode("hold");
                if (!prop.isNull()) {
                    instrument->hold_ = prop.value().toInt();
                }
            } else if (cur.tagName() == "arpeggio") {
                // Get arpeggio properties
                prop = cur.attributeNode("basenote");
                if (!prop.isNull()) {
                    instrument->arpeggioBaseNote_ = prop.value().toInt();
                }

                // Parse and add all block elements
                for (QDomElement temp = cur.firstChild().toElement(); !temp.isNull() && instrument->arpeggio_ == NULL; temp = temp.nextSibling().toElement()) {
                    if (temp.isElement()) {
                        instrument->arpeggio_ = Block::parse(temp);
                    }
                }
            }
            cur = cur.nextSibling().toElement();
        }
    } else if (element.nodeType() != QDomNode::CommentNode) {
        qWarning("XML error: expected instrument, got %s\n", element.tagName().toUtf8().constData());
    }

    return instrument;
}

void Instrument::save(int number, QDomElement &parentElement, QDomDocument &document)
{
    QDomElement instrumentElement = document.createElement("instrument");
    parentElement.appendChild(instrumentElement);
    instrumentElement.setAttribute("number", number);
    instrumentElement.setAttribute("name", name_);

    if (!midiInterfaceName().isEmpty()) {
        instrumentElement.setAttribute("midiinterface", midiInterfaceName());
    }
    instrumentElement.setAttribute("midipreset", midiPreset_);
    instrumentElement.setAttribute("midichannel", midiChannel_);
    instrumentElement.setAttribute("defaultvelocity", defaultVelocity_);
    instrumentElement.setAttribute("transpose", transpose_);
    instrumentElement.setAttribute("hold", hold_);

    // Add the arpeggio block if any
    if (arpeggio_ != NULL) {
        QDomElement arpeggioElement = document.createElement("arpeggio");
        arpeggioElement.appendChild(document.createTextNode("\n"));
        arpeggioElement.setAttribute("basenote", arpeggioBaseNote_);
        arpeggio_->save(0, arpeggioElement, document);

        instrumentElement.appendChild(arpeggioElement);
        instrumentElement.appendChild(document.createTextNode("\n"));
    }

    parentElement.appendChild(document.createTextNode("\n"));
}
