/*
 * song.cpp
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

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include "track.h"
#include "song.h"

Song::Song(const QString &path, QObject *parent) :
    QObject(parent),
    path_(path),
    modified(false)
{
    bool initialized = false;

    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QDomDocument doc;
            if (doc.setContent(&file)) {
                initialized = parse(doc.documentElement());
            }
            file.close();
        }
    }

    if (!initialized) {
        init();
    }
    checkMaxTracks();

    connect(this, SIGNAL(nameChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(blocksChanged(int)), this, SLOT(setModified()));
    connect(this, SIGNAL(playseqsChanged(int)), this, SLOT(setModified()));
    connect(this, SIGNAL(sectionsChanged(unsigned int)), this, SLOT(setModified()));
    connect(this, SIGNAL(messagesChanged(unsigned int)), this, SLOT(setModified()));
    connect(this, SIGNAL(maxTracksChanged(unsigned int)), this, SLOT(setModified()));
    connect(this, SIGNAL(playseqNameChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(blockNameChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(trackMutedOrSoloed()), this, SLOT(setModified()));
    connect(this, SIGNAL(trackNameChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(trackVolumeChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(blockLengthChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(sendSyncChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(masterVolumeChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(ticksPerLineChanged()), this, SLOT(setModified()));
    connect(this, SIGNAL(tempoChanged()), this, SLOT(setModified()));
}

Song::~Song()
{
    foreach(Playseq *playseq, playseqs_) {
        delete playseq;
    }
    foreach(Block *block, blocks_) {
        delete block;
    }
    foreach(Instrument *instrument, instruments_) {
        delete instrument;
    }
    foreach(Track *track, tracks) {
        delete track;
    }
    foreach(Message *message, messages_) {
        delete message;
    }
}

void Song::init()
{
    name_ = tr("Untitled");
    tempo_ = 120;
    ticksPerLine_ = 6;
    masterVolume_ = 127;
    sendSync_ = false;

    Block *block = new Block;
    connectBlockSignals(block);
    sections_.append(0);
    Playseq *playseq = new Playseq;
    connectPlayseqSignals(playseq);
    playseqs_.append(playseq);
    blocks_.append(block);
}

QString Song::name() const
{
    return name_;
}

void Song::setName(const QString &name)
{
    if (name_ != name) {
        name_ = name;

        emit nameChanged();
    }
}

unsigned int Song::tempo() const
{
    return tempo_;
}

unsigned int Song::ticksPerLine() const
{
    return ticksPerLine_;
}

bool Song::sendSync() const
{
    return sendSync_;
}

void Song::setSendSync(bool sendSync)
{
    if (sendSync_ != sendSync) {
        sendSync_ = sendSync;

        emit sendSyncChanged();
    }
}

unsigned int Song::masterVolume() const
{
    return masterVolume_;
}

void Song::setMasterVolume(unsigned int masterVolume)
{
    if (masterVolume_ != masterVolume) {
        masterVolume_ = masterVolume;

        emit masterVolumeChanged();
    }
}

QString Song::path() const
{
    return path_;
}

unsigned int Song::blocks() const
{
    return blocks_.count();
}

unsigned int Song::playseqs() const
{
    return playseqs_.count();
}

unsigned int Song::sections() const
{
    return sections_.count();
}

unsigned int Song::instruments() const
{
    return instruments_.count();
}

unsigned int Song::messages() const
{
    return messages_.count();
}

unsigned int Song::maxTracks() const
{
    return tracks.count();
}

Block *Song::block(unsigned int number) const
{
    return number < blocks_.count() ? blocks_[number] : NULL;
}

Track *Song::track(unsigned int number) const
{
    return number < tracks.count() ? tracks[number] : NULL;
}

Playseq *Song::playseq(unsigned int number) const
{
    return number < playseqs_.count() ? playseqs_[number] : NULL;
}

unsigned int Song::section(unsigned int pos) const
{
    return sections_[pos];
}

Instrument *Song::instrument(unsigned int number) const
{
    return number < instruments_.count() ? instruments_[number] : NULL;
}

Message *Song::message(unsigned int number) const
{
    return number < messages_.count() ? messages_[number] : NULL;
}

void Song::insertBlock(unsigned int pos, unsigned int current)
{
    mutex.lock();

    // Check block existence
    if (pos > blocks_.count()) {
        pos = blocks_.count();
    }
    if (current >= blocks_.count()) {
        current = blocks_.count() - 1;
    }

    // Insert a new block similar to the current block
    Block *block = new Block(blocks_[current]->tracks(), blocks_[current]->length(), blocks_[current]->commandPages());
    connectBlockSignals(block);
    blocks_.insert(pos, block);

    // Update playing sequences
    for (int i = 0; i < playseqs_.count(); i++) {
        for (unsigned int j = 0; j < playseqs_[i]->length(); j++) {
            if (playseqs_[i]->at(j) >= pos) {
                playseqs_[i]->set(j, playseqs_[i]->at(j) + 1);;
            }
        }
    }

    mutex.unlock();

    emit blocksChanged(blocks_.count());
}

void Song::deleteBlock(unsigned int pos)
{
    // Don't delete the last block
    if (blocks_.count() > 1) {
        mutex.lock();

        // Check block existence
        if (pos >= blocks_.count()) {
            pos = blocks_.count() - 1;
        }

        Block *block = blocks_.takeAt(pos);

        // Update playing sequences
        for (int i = 0; i < playseqs_.count(); i++) {
            for (int j = 0; j < playseqs_[i]->length(); j++) {
                if (playseqs_[i]->at(j) >= pos && playseqs_[i]->at(j) > 0) {
                    playseqs_[i]->set(j, playseqs_[i]->at(j) - 1);
                }
            }
        }

        mutex.unlock();

        emit blocksChanged(blocks_.count());

        delete block;
    }
}

void Song::splitBlock(unsigned int pos, unsigned int line)
{
    mutex.lock();

    // Check block existence
    if (pos > blocks_.count()) {
        pos = blocks_.count();
    }

    // Insert a new block similar to the current block
    Block *block = blocks_[pos]->split(line);
    if (block != NULL) {
        connectBlockSignals(block);
        blocks_.insert(pos + 1, block);

        // Update playing sequences
        for (int i = 0; i < playseqs_.count(); i++) {
            for (unsigned int j = 0; j < playseqs_[i]->length(); j++) {
                if (playseqs_[i]->at(j) > pos) {
                    playseqs_[i]->set(j, playseqs_[i]->at(j) + 1);;
                }
            }
        }
    }

    mutex.unlock();

    emit blocksChanged(blocks_.count());
}

// Inserts a new playseq in the playseq array in the given position
void Song::insertPlayseq(unsigned int pos)
{
    mutex.lock();

    // Check playseq existence
    if (pos > playseqs_.count()) {
        pos = playseqs_.count();
    }

    // Insert a new playing sequence
    Playseq *playseq = new Playseq;
    connectPlayseqSignals(playseq);
    playseqs_.insert(pos, playseq);

    // Update sections
    for (int i = 0; i < sections_.count(); i++) {
        if (sections_[i] >= pos) {
            sections_[i]++;
        }
    }

    mutex.unlock();

    emit playseqsChanged(playseqs_.count());
}

void Song::deletePlayseq(unsigned int pos)
{
    // Don't delete the last playseq
    if (playseqs_.count() > 1) {
        mutex.lock();

        // Check playseq existence
        if (pos >= playseqs_.count()) {
            pos = playseqs_.count() - 1;
        }

        Playseq *playseq = playseqs_.takeAt(pos);

        // Update section lists
        for (int i = 0; i < sections_.count(); i++) {
            if (sections_[i] >= pos && sections_[i] > 0) {
                sections_[i]--;
            }
        }

        mutex.unlock();

        emit playseqsChanged(playseqs_.count());

        delete playseq;
    }
}

void Song::insertSection(unsigned int pos)
{
    mutex.lock();

    // Check that the value is possible
    if (pos > sections_.count()) {
        pos = sections_.count();
    }

    sections_.insert(pos, pos < sections_.count() ? sections_[pos] : sections_[sections_.count() - 1]);

    mutex.unlock();

    emit sectionsChanged(sections_.count());
}

void Song::deleteSection(unsigned int pos)
{
    // Don't delete the last section
    if (sections_.count() > 1) {
        mutex.lock();

        // Check section existence
        if (pos >= sections_.count()) {
            pos = sections_.count() - 1;
        }

        sections_.removeAt(pos);

        mutex.unlock();
    }

    emit sectionsChanged(sections_.count());
}

void Song::insertMessage(unsigned int pos)
{
    // Check message existence
    if (pos > messages_.count()) {
        pos = messages_.count();
    }

    // Insert a new message
    messages_.insert(pos, new Message());

    emit messagesChanged(messages_.count());
}

void Song::deleteMessage(unsigned int pos)
{
    // Don't delete inexisting messages
    if (messages_.count() > 0) {
        // Check message existence
        if (pos >= messages_.count()) {
            pos = messages_.count() - 1;
        }

        // Free the message in question
        messages_.removeAt(pos);
    }

    emit messagesChanged(messages_.count());
}

void Song::setSection(unsigned int pos, unsigned int playseq)
{
    if (pos < sections_.count() && playseq < playseqs_.count()) {
        mutex.lock();

        sections_[pos] = playseq;

        mutex.unlock();

        setModified();
    }
}

void Song::setTPL(int ticksPerLine)
{
    if (ticksPerLine_ != ticksPerLine) {
        ticksPerLine_ = ticksPerLine;

        emit ticksPerLineChanged();
    }
}

void Song::setTempo(int tempo)
{
    if (tempo_ != tempo) {
        tempo_ = tempo;

        emit tempoChanged();
    }
}

void Song::checkMaxTracks()
{
    int max = 0;
    int oldMax = tracks.count();

    // Check the maximum number of tracks;
    for (int track = 0; track < blocks_.count(); track++) {
        if (blocks_[track]->tracks() > max) {
            max = blocks_[track]->tracks();
        }
    }

    if (oldMax < max) {
        while (tracks.count() < max) {
            // Give a descriptive name for each new track
            addTrack(-1, tr("Track %1").arg(tracks.count() + 1));
        }
    } else if (oldMax > max) {
        // Tracks removed: free track datas
        while (tracks.count() > max) {
            delete tracks.takeLast();
        }
     }

    if (max != oldMax) {
        emit maxTracksChanged(max);
    }
}

void Song::checkInstrument(int instrument)
{
    while (instrument >= instruments_.count()) {
        Instrument *instrument = new Instrument(tr("Unnamed"));
        connectInstrumentSignals(instrument);
        instruments_.append(instrument);
    }
}

void Song::transpose(int instrument, int halfNotes)
{
    for (int block = 0; block < blocks_.count(); block++) {
        blocks_[block]->transpose(instrument, halfNotes, 0, 0, blocks_[block]->tracks() - 1, blocks_[block]->length() - 1);
    }
}

void Song::expandShrink(int factor, bool changeBlockLength)
{
    for (int block = 0; block < blocks_.count(); block++) {
        blocks_[block]->expandShrink(factor, 0, 0, blocks_[block]->tracks() - 1, blocks_[block]->length() - 1, changeBlockLength);
    }
}

void Song::changeInstrument(int from, int to, bool swap)
{
    for (int block = 0; block < blocks_.count(); block++) {
        blocks_[block]->changeInstrument(from, to, swap, 0, 0, blocks_[block]->tracks() - 1, blocks_[block]->length() - 1);
    }
}

void Song::insertTrack(int track)
{
    addTrack(track, tr("Track %1").arg(track + 1));
    for (int block = 0; block < blocks_.count(); block++) {
        disconnect(blocks_[block], SIGNAL(tracksChanged(int)), this, SLOT(checkMaxTracks()));
        blocks_[block]->insertTrack(track);
        connect(blocks_[block], SIGNAL(tracksChanged(int)), this, SLOT(checkMaxTracks()));
    }

    // Maximum number of tracks will have changed since a track has been added to every block
    emit maxTracksChanged(tracks.count());

    emit tracksChanged();
}

void Song::deleteTrack(int track)
{
    if (maxTracks() > 1) {
        for (int block = 0; block < blocks_.count(); block++) {
            disconnect(blocks_[block], SIGNAL(tracksChanged(int)), this, SLOT(checkMaxTracks()));
            blocks_[block]->deleteTrack(track);
            connect(blocks_[block], SIGNAL(tracksChanged(int)), this, SLOT(checkMaxTracks()));
        }
        Track *trackToBeDeleted = tracks.takeAt(track);

        // Maximum number of tracks will have changed since a track has been deleted from every block
        emit maxTracksChanged(tracks.count());

        emit tracksChanged();

        delete trackToBeDeleted;
    }
}

bool Song::parse(QDomElement element)
{
    if (element.tagName() == "song") {
        QDomAttr prop;

        prop = element.attributeNode("name");
        if (!prop.isNull()) {
            name_ = prop.value();
        }

        prop = element.attributeNode("tempo");
        if (!prop.isNull()) {
            tempo_ = prop.value().toInt();
        }

        prop = element.attributeNode("ticksperline");
        if (!prop.isNull()) {
            ticksPerLine_ = prop.value().toInt();
        }

        prop = element.attributeNode("mastervolume");
        if (!prop.isNull()) {
            masterVolume_ = prop.value().toInt();
        }

        prop = element.attributeNode("sendsync");
        if (!prop.isNull()) {
            sendSync_ = (prop.value().toInt() == 1);
        }

        QDomElement cur = element.firstChild().toElement();
        while(!cur.isNull()) {
            if (cur.tagName() == "blocks") {
                // Parse and add all block elements
                QDomElement temp = cur.firstChild().toElement();

                while (!temp.isNull()) {
                    if (temp.isElement()) {
                        int number = -1;
                        Block *block = Block::parse(temp);

                        if (block != NULL) {
                            connectBlockSignals(block);
                            prop = temp.attributeNode("number");
                            if (!prop.isNull()) {
                                number = prop.value().toInt();
                            }

                            while (blocks_.count() < number) {
                                Block *fillBlock = new Block;
                                connectBlockSignals(fillBlock);
                                blocks_.append(fillBlock);
                            }
                            if (blocks_.count() == number) {
                                blocks_.append(block);
                            } else {
                                delete blocks_.takeAt(number);
                                blocks_.insert(number, block);
                            }
                        }
                    }

                    temp = temp.nextSibling().toElement();
                }
            } else if (cur.tagName() == "sections") {
                // Parse and add all section elements
                QDomElement temp = cur.firstChild().toElement();

                while (!temp.isNull()) {
                    if (temp.isElement()) {
                        int number = -1;

                        // The section number is required
                        if (temp.tagName() == "section") {
                            prop = temp.attributeNode("number");
                            if (!prop.isNull()) {
                                number = prop.value().toInt();
                            }

                            // Get playing sequence
                            while (sections_.count() < number) {
                                sections_.append(0);
                            }
                            if (sections_.count() == number) {
                                sections_.append(temp.text().toInt());
                            } else {
                                sections_.replace(number, temp.text().toInt());
                            }
                        } else if (temp.nodeType() != QDomNode::CommentNode) {
                            qWarning("XML error: expected section, got %s\n", temp.tagName().toUtf8().constData());
                        }
                    }

                    temp = temp.nextSibling().toElement();
                }
            } else if (cur.tagName() == "playingsequences") {
                // Parse and add all playingsequence elements

                QDomElement temp = cur.firstChild().toElement();
                while (!temp.isNull()) {
                    if (temp.isElement()) {
                        int number = -1;
                        Playseq *playseq = Playseq::parse(temp);

                        if (playseq != NULL) {
                            connectPlayseqSignals(playseq);
                            prop = temp.attributeNode("number");
                            if (!prop.isNull()) {
                                number = prop.value().toInt();
                            }

                            while (playseqs_.count() < number) {
                                Playseq *playseq = new Playseq;
                                connectPlayseqSignals(playseq);
                                playseqs_.append(playseq);
                            }
                            if (playseqs_.count() == number) {
                                playseqs_.append(playseq);
                            } else {
                                delete playseqs_.takeAt(number);
                                playseqs_.insert(number, playseq);
                            }
                        }
                    }

                    temp = temp.nextSibling().toElement();
                }
            } else if (cur.tagName() == "instruments") {
                // Parse and add all instrument elements
                QDomElement temp = cur.firstChild().toElement();

                while (!temp.isNull()) {
                    if (temp.isElement()) {
                        int number = -1;
                        Instrument *instrument = Instrument::parse(temp);

                        if (instrument != NULL) {
                            connectInstrumentSignals(instrument);
                            prop = temp.attributeNode("number");
                            if (!prop.isNull()) {
                                number = prop.value().toInt();
                            }

                            while (instruments_.count() < number) {
                                Instrument *instrument = new Instrument;
                                connectInstrumentSignals(instrument);
                                instruments_.append(instrument);
                            }
                            if (instruments_.count() == number) {
                                instruments_.append(instrument);
                            } else {
                                delete instruments_.takeAt(number);
                                instruments_.insert(number, instrument);
                            }
                        }
                    }

                    temp = temp.nextSibling().toElement();
                }
            } else if (cur.tagName() == "tracks") {
                // Parse and add all track elements
                QDomElement temp = cur.firstChild().toElement();

                while (!temp.isNull()) {
                    if (temp.isElement()) {
                        int track = -1;

                        // The track number is required
                        if (temp.tagName() == "track") {
                            prop = temp.attributeNode("number");
                            if (!prop.isNull()) {
                                track = prop.value().toInt();
                            }

                            while (tracks.count() <= track) {
                                addTrack();
                            }

                            // Get volume, mute, solo and name
                            prop = temp.attributeNode("volume");
                            if (!prop.isNull()) {
                                tracks[track]->setVolume(prop.value().toInt());
                            }

                            prop = temp.attributeNode("mute");
                            if (!prop.isNull()) {
                                tracks[track]->setMute(prop.value().toInt() > 0);
                            }

                            prop = temp.attributeNode("solo");
                            if (!prop.isNull()) {
                                tracks[track]->setSolo(prop.value().toInt() > 0);
                            }

                            tracks[track]->setName(temp.text());
                        } else if (temp.nodeType() != QDomNode::CommentNode) {
                            qWarning("XML error: expected section, got %s\n", temp.tagName().toUtf8().constData());
                        }
                    }

                    temp = temp.nextSibling().toElement();
                }
            } else if (cur.tagName() == "trackvolumes") {
                // Backwards compatibility: parse and add all track volume elements
                QDomElement temp = cur.firstChild().toElement();

                while (!temp.isNull()) {
                    if (temp.isElement()) {
                        int track = -1;

                        // The track number is required
                        if (temp.tagName() == "trackvolume") {
                            prop = temp.attributeNode("track");
                            if (!prop.isNull()) {
                                track = prop.value().toInt();
                            }

                            while (tracks.count() < track) {
                                addTrack();
                            }

                            // Get the volume
                            QDomElement temp2 = temp.firstChild().toElement();
                            if (!temp2.isNull()) {
                                tracks[track]->setVolume(temp2.text().toInt() & 127);
                                tracks[track]->setMute((temp2.text().toInt() & 128) > 0);
                            }
                        } else if (temp.nodeType() != QDomNode::CommentNode) {
                            qWarning("XML error: expected trackvolume, got %s\n", temp.tagName().toUtf8().constData());
                        }
                    }

                    temp = temp.nextSibling().toElement();
                }
            } else if (cur.tagName() == "messages") {
                // Parse and add all Message elements
                QDomElement temp = cur.firstChild().toElement();

                while (!temp.isNull()) {
                    if (temp.isElement()) {
                        int number = -1;
                        Message *message = Message::parse(temp);

                        if (message != NULL) {
                            prop = temp.attributeNode("number");
                            if (!prop.isNull()) {
                                number = prop.value().toInt();
                            }

                            while (messages_.count() < number) {
                                messages_.append(new Message);
                            }
                            if (messages_.count() == number) {
                                messages_.append(message);
                            } else {
                                delete messages_.takeAt(number);
                                messages_.insert(number, message);
                            }
                        }
                    }

                    temp = temp.nextSibling().toElement();
                }
            }
            cur = cur.nextSibling().toElement();
        }
        return true;
    } else {
        qWarning("XML error: expected song, got %s\n", element.tagName().toUtf8().constData());
        return false;
    }
}

void Song::save(const QString &path)
{
    path_ = path;

    QDomDocument document;
    QDomElement songElement = document.createElement("song");
    document.appendChild(songElement);

    songElement.setAttribute("name", name_);
    songElement.setAttribute("tempo", tempo_);
    songElement.setAttribute("ticksperline", ticksPerLine_);
    songElement.setAttribute("mastervolume", masterVolume_);
    songElement.setAttribute("sendsync", sendSync_ ? 1 : 0);
    songElement.appendChild(document.createTextNode("\n\n"));

    QDomElement blocksElement = document.createElement("blocks");
    songElement.appendChild(blocksElement);
    blocksElement.appendChild(document.createTextNode("\n"));
    // Add all blocks
    for (int block = 0; block < blocks_.count(); block++) {
        blocks_[block]->save(block, blocksElement, document);
    }
    songElement.appendChild(document.createTextNode("\n\n"));

    QDomElement sectionsElement = document.createElement("sections");
    songElement.appendChild(sectionsElement);
    sectionsElement.appendChild(document.createTextNode("\n"));
    // Add all sections
    for (int section = 0; section < sections_.count(); section++) {
        QDomElement sectionElement = document.createElement("section");
        sectionElement.appendChild(document.createTextNode(QString("%1").arg(sections_[section])));
        sectionElement.setAttribute("number", section);
        sectionsElement.appendChild(sectionElement);
        sectionsElement.appendChild(document.createTextNode("\n"));
    }
    songElement.appendChild(document.createTextNode("\n\n"));

    QDomElement playingSequencesElement = document.createElement("playingsequences");
    songElement.appendChild(playingSequencesElement);
    playingSequencesElement.appendChild(document.createTextNode("\n"));
    // Add all playing sequences
    for (int playseq = 0; playseq < playseqs_.count(); playseq++) {
        playseqs_[playseq]->save(playseq, playingSequencesElement, document);
    }
    songElement.appendChild(document.createTextNode("\n\n"));

    QDomElement instrumentsElement = document.createElement("instruments");
    songElement.appendChild(instrumentsElement);
    instrumentsElement.appendChild(document.createTextNode("\n"));
    // Add all instruments
    for (int instrument = 0; instrument < instruments_.count(); instrument++) {
        instruments_[instrument]->save(instrument, instrumentsElement, document);
    }
    songElement.appendChild(document.createTextNode("\n\n"));

    QDomElement tracksElement = document.createElement("tracks");
    songElement.appendChild(tracksElement);
    tracksElement.appendChild(document.createTextNode("\n"));
    // Add all tracks
    for (int track = 0; track < tracks.count(); track++) {
        QDomElement trackElement = document.createElement("track");
        if (!tracks[track]->name().isEmpty()) {
            trackElement.appendChild(document.createTextNode(tracks[track]->name()));
        }
        trackElement.setAttribute("number", track);
        trackElement.setAttribute("volume", tracks[track]->volume());
        trackElement.setAttribute("mute", tracks[track]->isMuted() ? 1 : 0);
        trackElement.setAttribute("solo", tracks[track]->isSolo() ? 1 : 0);
        tracksElement.appendChild(trackElement);
        tracksElement.appendChild(document.createTextNode("\n"));
    }
    songElement.appendChild(document.createTextNode("\n\n"));

    QDomElement messagesElement = document.createElement("messages");
    songElement.appendChild(messagesElement);
    messagesElement.appendChild(document.createTextNode("\n"));
    // Add all messages
    for (int message = 0; message < messages_.count(); message++) {
        messages_[message]->save(message, messagesElement, document);
    }
    songElement.appendChild(document.createTextNode("\n\n"));

    QFile file(path_);
    file.open(QIODevice::WriteOnly);
    file.write("<?xml version=\"1.0\"?>\n");
    file.write(document.toByteArray());
    file.close();

    setModified(false);
}

void Song::lock()
{
    mutex.lock();
}

void Song::unlock()
{
    mutex.unlock();
}

void Song::addTrack(int index, const QString &name)
{
    Track *track = new Track(name);
    connect(track, SIGNAL(mutedChanged(bool)), this, SIGNAL(trackMutedOrSoloed()));
    connect(track, SIGNAL(soloChanged(bool)), this, SIGNAL(trackMutedOrSoloed()));
    connect(track, SIGNAL(nameChanged(QString)), this, SIGNAL(trackNameChanged()));
    connect(track, SIGNAL(volumeChanged(int)), this, SIGNAL(trackVolumeChanged()));
    if (index < 0 || index > tracks.count()) {
        index = tracks.count();
    }
    tracks.insert(index, track);
}

void Song::connectBlockSignals(Block *block)
{
    connect(block, SIGNAL(tracksChanged(int)), this, SLOT(checkMaxTracks()));
    connect(block, SIGNAL(lengthChanged(int)), this, SIGNAL(blockLengthChanged()));
    connect(block, SIGNAL(nameChanged(QString)), this, SIGNAL(blockNameChanged()));
    connect(block, SIGNAL(areaChanged(int, int, int, int)), this, SLOT(setModified()));
    connect(block, SIGNAL(tracksChanged(int)), this, SLOT(setModified()));
    connect(block, SIGNAL(commandPagesChanged(int)), this, SLOT(setModified()));
}

void Song::connectPlayseqSignals(Playseq *playseq)
{
    connect(playseq, SIGNAL(nameChanged(QString)), this, SIGNAL(playseqNameChanged()));
    connect(playseq, SIGNAL(lengthChanged()), this, SLOT(setModified()));
    connect(playseq, SIGNAL(blocksChanged()), this, SLOT(setModified()));
}

void Song::connectInstrumentSignals(Instrument *instrument)
{
    connect(instrument, SIGNAL(nameChanged(QString)), this, SLOT(setModified()));
}

bool Song::isModified() const
{
    return modified;
}

void Song::setModified(bool modified)
{
    if (this->modified != modified) {
        this->modified = modified;

        emit modifiedChanged();
    }
}
