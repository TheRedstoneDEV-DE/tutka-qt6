/*
 * player.cpp
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

#include <cstddef>
#include <cstdlib>
#include <sys/time.h>
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <QFile>
#include "song.h"
#include "track.h"
#include "instrument.h"
#include "midiinterface.h"
#include "midi.h"
#include "mmd.h"
#include "conversion.h"
#include "scheduler.h"
#include "player.h"

Player::Player(MIDI *midi, const QString &path, QObject *parent) :
    QThread(parent),
    section_(0),
    playseq_(0),
    position_(0),
    block_(0),
    line_(0),
    tick(0),
    song(NULL),
    oldSong(NULL),
    mode_(ModeIdle),
    scheduler(NULL),
    syncMode(Off),
    ticksSoFar(0),
    externalSyncTicks(0),
    killThread(false),
    midi_(midi),
    solo(0),
    postCommand(0),
    postValue(0),
    tempoChanged(false),
    killWhenLooped(false)
{
    connect(midi, SIGNAL(outputsChanged()), this, SLOT(remapMidiOutputs()));
    connect(midi, SIGNAL(startReceived()), this, SLOT(playSong()));
    connect(midi, SIGNAL(continueReceived()), this, SLOT(continueSong()));
    connect(midi, SIGNAL(stopReceived()), this, SLOT(stop()));
    connect(midi, SIGNAL(clockReceived()), this, SLOT(externalSync()));

    setSong(path);
}

Player::Player(MIDI *midi, Song *song, bool from_export, QObject *parent) :
    QThread(parent),
    section_(0),
    playseq_(0),
    position_(0),
    block_(0),
    line_(0),
    tick(0),
    song(song),
    mode_(ModeIdle),
    scheduler(NULL),
    syncMode(Off),
    ticksSoFar(0),
    externalSyncTicks(0),
    killThread(false),
    midi_(midi),
    solo(0),
    postCommand(0),
    postValue(0),
    tempoChanged(false),
    killWhenLooped(false),
    from_export(from_export)
{
    connect(midi, SIGNAL(outputsChanged()), this, SLOT(remapMidiOutputs()));
    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(stop()));
    QTimer::singleShot(0, this, SLOT(init()));
}

Player::~Player()
{
    // Stop the player
    stop();
    wait();
}

void Player::updateLocation(bool alwaysSendLocationSignals)
{
    unsigned int oldSection = section_;
    unsigned int oldPlayseq = playseq_;
    unsigned int oldPosition = position_;
    unsigned int oldBlock = block_;

    if (section_ >= song->sections()) {
        section_ = 0;
    }

    unsigned int playseq = song->section(section_);
    if (playseq >= song->playseqs()) {
        playseq = song->playseqs() - 1;
    }
    playseq_ = playseq;

    if (position_ >= song->playseq(playseq_)->length()) {
        position_ = 0;
    }

    unsigned int block = song->playseq(playseq_)->at(position_);
    if (block >= song->blocks()) {
        block = song->blocks() - 1;
    }
    block_ = block;

    if (section_ != oldSection || alwaysSendLocationSignals) {
        emit sectionChanged(section_);
    }
    if (playseq_ != oldPlayseq || alwaysSendLocationSignals) {
        emit playseqChanged(playseq_);
    }
    if (position_ != oldPosition || alwaysSendLocationSignals) {
        emit positionChanged(position_);
    }
    if (block_ != oldBlock || alwaysSendLocationSignals) {
        emit blockChanged(block_);
    }
}

void Player::resetSection()
{
    unsigned int oldSection = section_;

    setSection(section_);

    if (section_ == oldSection) {
        // Emit signal regardless of whether there's a change or not
        emit sectionChanged(section_);
    }
}

void Player::resetPlayseq()
{
    unsigned int oldPlayseq = playseq_;

    setPlayseq(playseq_);

    if (playseq_ == oldPlayseq) {
        // Emit signal regardless of whether there's a change or not
        emit playseqChanged(playseq_);
    }
}

void Player::resetBlock()
{
    unsigned int oldBlock = block_;

    setBlock(block_);

    if (block_ == oldBlock) {
        // Emit signal regardless of whether there's a change or not
        emit blockChanged(block_);
    }
}

void Player::resetLine()
{
    unsigned int oldLine = line_;

    setLine(line_, false);

    if (line_ == oldLine) {
        // Emit signal regardless of whether there's a change or not
        emit lineChanged(line_);
    }
}

bool Player::nextSection()
{
    unsigned int oldSection = section_;
    section_++;

    bool looped = section_ >= song->sections();
    if (looped) {
        section_ = 0;
    }

    if (section_ != oldSection) {
        emit sectionChanged(section_);
    }

    return looped;
}

bool Player::nextPosition()
{
    unsigned int oldPosition = position_;
    position_++;

    bool looped = position_ >= song->playseq(playseq_)->length();
    if (looped) {
        position_ = 0;
    }

    if (position_ != oldPosition) {
        emit positionChanged(position_);
    }

    return looped ? nextSection() : false;
}

void Player::playNote(unsigned int instrumentNumber, unsigned char note, unsigned char volume, unsigned char track, bool postpone)
{
    // Notes are played if the track is not muted and no tracks are soloed or the current track is soloed
    if (!song->track(track)->isMuted() && (!solo || (solo && song->track(track)->isSolo()))) {
        QSharedPointer<TrackStatus> trackStatus = trackStatuses[track];

        // Stop currently playing note
        if (trackStatus->note != -1) {
            midi_->output(trackStatus->midiInterface)->noteOff(trackStatus->midiChannel, trackStatus->note, 127);
            trackStatus->note = -1;
        }

        // Don't play a note if the instrument does not exist
        Instrument *instrument = song->instrument(instrumentNumber);
        if (instrument != NULL) {
            trackStatus->instrument = instrumentNumber;

            // Update track status for the selected output
            trackStatus->volume = instrument->defaultVelocity() * volume / 127 * song->track(track)->volume() / 127 * song->masterVolume() / 127;
            trackStatus->midiChannel = instrument->midiChannel();
            trackStatus->midiInterface = instrument->midiInterface();
            trackStatus->hold = instrument->hold() > 0 ? instrument->hold() : -1;

            // Make sure the volume isn't too large
            if (trackStatus->volume < 0) {
                trackStatus->volume = 127;
            }

            if (trackStatus->volume != 0) {
                // Play note
                trackStatus->note = note + instrument->transpose();
                if (postpone) {
                    postponedNotes.append(NoteOn(instrument->midiInterface(), trackStatus->midiChannel, trackStatus->note, trackStatus->volume));
                } else {
                    midi_->output(instrument->midiInterface())->noteOn(trackStatus->midiChannel, trackStatus->note, trackStatus->volume);
                }
            } else {
                trackStatus->note = -1;
            }
        }
    }
}

void Player::stopMuted()
{
    for (int track = 0; track < song->maxTracks(); track++) {
        if (song->track(track)->isMuted() || (solo && !song->track(track)->isSolo())) {
            QSharedPointer<TrackStatus> trackStatus = trackStatuses[track];
            if (trackStatus->note != -1) {
                midi_->output(trackStatus->midiInterface)->noteOff(trackStatus->midiChannel, trackStatus->note, 127);
            }
            trackStatus->reset();
        }
    }
}

void Player::stopNotes()
{
    if (song == NULL) {
        return;
    }

    for (int track = 0; track < song->maxTracks(); track++) {
        QSharedPointer<TrackStatus> trackStatus = trackStatuses[track];
        if (trackStatus->note != -1) {
            midi_->output(trackStatus->midiInterface)->noteOff(trackStatus->midiChannel, trackStatus->note, 127);
        }
        trackStatus->reset();
    }
}

void Player::stopAllNotes()
{
    for (int midiChannel = 0; midiChannel < 16; midiChannel++) {
        for (int note = 0; note < 128; note++) {
            for (int output = 0; output < midi_->outputs(); output++) {
                midi_->output(output)->noteOff(midiChannel, note, 127);
            }
        }
    }
}

void Player::resetPitch()
{
    for (int midiChannel = 0; midiChannel < 16; midiChannel++) {
        for (int output = 0; output < midi_->outputs(); output++) {
            midi_->output(output)->pitchWheel(midiChannel, 64);
        }
    }
}

void Player::handleCommand(QSharedPointer<TrackStatus> trackStatus, unsigned char note, unsigned char instrument, unsigned char command, unsigned char value, unsigned int *volume, int *delay, int *repeat, int *hold)
{
    if (command == 0 && value == 0) {
        return;
    }

    int midiInterface;
    int midiChannel;

    // Check which MIDI interface/channel pairs the command will affect
    if (instrument != 0) {
        // Instrument number defines MIDI interfaces/channels
        midiInterface = song->instrument(instrument - 1)->midiInterface();
        midiChannel = song->instrument(instrument - 1)->midiChannel();
    } else {
        // Note playing defines MIDI interfaces/channels
        midiInterface = trackStatus->midiInterface;
        midiChannel = trackStatus->midiChannel;
    }

    // If the MIDI interface is not known, use the null output interface
    QSharedPointer<MIDIInterface> output = midi_->output(midiInterface >= 0 ? midiInterface : 0);

    // Check for previous command if any
    if (command == CommandPreviousCommandValue) {
        if (value != 0) {
            command = trackStatus->previousCommand;
        }
    } else {
        trackStatus->previousCommand = command;
    }

    Track *track = song->track(trackStatus->track);
    switch (command) {
    case CommandPitchWheel:
        // Pitch wheel can be set if the MIDI channel is known
        if (midiChannel != -1) {
            if (value < 0x80) {
                if (tick == 0) {
                    output->pitchWheel(midiChannel, value);
                    midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_PITCH_WHEEL] = value;
                }
            } else {
                if (tick < song->ticksPerLine() - 1) {
                    float delta = (value - 0x80 - midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_PITCH_WHEEL]) / (float)song->ticksPerLine();
                    output->pitchWheel(midiChannel, midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_PITCH_WHEEL] + (tick + 1) * delta);
                } else {
                    output->pitchWheel(midiChannel, value - 0x80);
                    midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_PITCH_WHEEL] = value - 0x80;
                }
            }
        }
        break;
    case CommandProgramChange:
        // Program change can be sent if the MIDI channel is known
        if (midiChannel != -1 && tick == 0) {
            output->programChange(midiChannel, value & 0x7f);
        }
        break;
    case CommandEndBlock:
        // Only on last tick
        if (tick == song->ticksPerLine() - 1) {
            postCommand = CommandEndBlock;
            postValue = value;
        }
        break;
    case CommandPlayseqPosition:
        // Only on last tick
        if (tick == song->ticksPerLine() - 1) {
            postCommand = CommandPlayseqPosition;
            postValue = value;
        }
        break;
    case CommandSendMessage:
        // Only on first tick
        if (tick == 0 && value < song->messages()) {
            output->writeRaw(song->message(value)->data());
        }
        break;
    case CommandHold:
        *hold = value;
        break;
    case CommandRetrigger:
        *delay = (value & 0xf0) >> 4;
        *repeat = value & 0x0f;
        break;
    case CommandDelay:
        *delay = value;
        *repeat = -1;
        break;
    case CommandVelocity:
        if (note != 0) {
            *volume = value;
            if (midiChannel != -1) {
                midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_AFTERTOUCH] = value;
            }
        } else {
            // Note playing defines MIDI channel
            midiChannel = trackStatus->midiChannel;

            if (midiChannel != -1) {
                if (value < 0x80) {
                    if (tick == 0) {
                        if (value > 0) {
                            output->aftertouch(midiChannel, trackStatus->note, value);
                            midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_AFTERTOUCH] = value;
                        } else {
                            output->noteOff(midiChannel, trackStatus->note, 127);
                            trackStatus->note = -1;
                            trackStatus->line = -1;
                        }
                    }
                } else {
                    if (tick < song->ticksPerLine() - 1) {
                        float delta = (value - 0x80 - midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_AFTERTOUCH]) / (float)song->ticksPerLine();
                        output->aftertouch(midiChannel, trackStatus->note, midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_AFTERTOUCH] + (tick + 1) * delta);
                    } else {
                        output->aftertouch(midiChannel, trackStatus->note, value - 0x80);
                        midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_AFTERTOUCH] = value - 0x80;
                    }
                }
            }
        }
        break;
    case CommandChannelPressure:
        // Channel pressure can be set if the MIDI channel is known
        if (midiChannel != -1) {
            if (value < 0x80) {
                if (tick == 0) {
                    output->channelPressure(midiChannel, value);
                    midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_CHANNEL_PRESSURE] = value;
                }
            } else {
                if (tick < song->ticksPerLine() - 1) {
                    float delta = (value - 0x80 - midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_CHANNEL_PRESSURE]) / (float)song->ticksPerLine();
                    output->channelPressure(midiChannel, midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_CHANNEL_PRESSURE] + (tick + 1) * delta);
                } else {
                    output->channelPressure(midiChannel, value - 0x80);
                    midiControllerValues[midiInterface][midiChannel * VALUES + VALUES_CHANNEL_PRESSURE] = value - 0x80;
                }
            }
        }
        break;
    case CommandTicksPerLine:
        if (value == 0) {
            // Only on last tick
            if (tick == song->ticksPerLine() - 1) {
                postCommand = CommandTicksPerLine;
            }
        } else {
            song->setTPL(value);
        }
        break;
    case CommandTempo:
        if (value == 0) {
            // Only on last tick
            if (tick == song->ticksPerLine() - 1) {
                postCommand = CommandTempo;
            }
        } else {
            song->setTempo(value);
            output->tempo(value);
        }
        break;
    case CommandTrackVolume:
        if (value < 0x80) {
            if (tick == 0) {
                track->setVolume(value);
            }
        } else {
            if (tick < song->ticksPerLine() - 1) {
                float delta = (value - 0x80 - track->volume()) / (float)song->ticksPerLine();
                track->setVolume(track->volume() + (tick + 1) * delta);
            } else {
                track->setVolume(value - 0x80);
            }
        }
        break;
    case CommandInstrumentVolume: {
        char trackInstrument = instrument != 0 ? (instrument - 1) : trackStatus->instrument;
        Instrument *instrument = song->instrument(trackInstrument);
        if (instrument != NULL) {
            if (value < 0x80) {
                if (tick == 0) {
                    instrument->setDefaultVelocity(value);
                }
            } else {
                if (tick < song->ticksPerLine() - 1) {
                    float delta = (value - 0x80 - instrument->defaultVelocity()) / (float)song->ticksPerLine();
                    instrument->setDefaultVelocity(track->volume() + (tick + 1) * delta);
                } else {
                    instrument->setDefaultVelocity(value - 0x80);
                }
            }
        }
        break;
    }
    }

    // Handle MIDI controllers
    if (command >= CommandMidiControllers) {
        // MIDI controllers can be set if the MIDI channel is known
        if (midiChannel != -1) {
            if (value < 0x80) {
                if (tick == 0) {
                    output->controller(midiChannel, command - CommandMidiControllers, value);
                    midiControllerValues[midiInterface][midiChannel * VALUES + command - CommandMidiControllers] = value;
                }
            } else {
                if (tick < song->ticksPerLine() - 1) {
                    float delta = (value - 0x80 - midiControllerValues[midiInterface][midiChannel * VALUES + command - CommandMidiControllers]) / (float)song->ticksPerLine();
                    output->controller(midiChannel, command - CommandMidiControllers, midiControllerValues[midiInterface][midiChannel * VALUES + command - CommandMidiControllers] + (tick + 1) * delta);
                } else {
                    output->controller(midiChannel, command - CommandMidiControllers, value - 0x80);
                    midiControllerValues[midiInterface][midiChannel * VALUES + command - CommandMidiControllers] = value - 0x80;
                }
            }
        }
    }
}

bool shouldPlayNote(unsigned int tick, int delay, int repeat)
{
    return (delay >= 0 && tick == delay) || (repeat == 0 && tick == 0) || (repeat > 0 && tick >= delay && (tick - delay) % repeat == 0);
}

void Player::run()
{
    ExternalSync prevsyncMode = syncMode;
    unsigned int oldTime = (unsigned int)-1;
    unsigned int oldLine = line_;

    tick = 0;
    ticksSoFar = 0;

    if (scheduler != NULL) {
        scheduler->start(playingStarted);
    }

    while (true) {
        bool looped = false;
        oldLine = line_;

        // Lock
        mutex.lock();
        song->lock();

        if (syncMode != Off) {
            if (externalSyncTicks == 0) {
                // Wait for a sync signal to come in
                externalSync_.wait(&mutex);
            }
            if (externalSyncTicks > 0) {
                externalSyncTicks--;
            }
        } else if (scheduler != NULL) {
            song->unlock();
            mutex.unlock();

            scheduler->waitForTick(song, syncMode != prevsyncMode);
            prevsyncMode = syncMode;

            mutex.lock();
            song->lock();
        }

        // Handle this tick
        for (int output = 0; output < midi_->outputs(); output++) {
            midi_->output(output)->setTick(ticksSoFar);
        }

        Block *block = song->block(block_);
        int commandPages = block->commandPages();

        // Send MIDI sync if requested
        if (song->sendSync()) {
            for (int output = 0; output < midi_->outputs(); output++) {
                midi_->output(output)->clock();
            }
        }

        // The block may have changed, make sure the line won't overflow
        if (line_ >= block->length()) {
            line_ %= block->length();
        }

        for (int track = 0; track < block->tracks(); track++) {
            QSharedPointer<TrackStatus> trackStatus = trackStatuses[track];

            // The track is taken into account if the track is not muted and no tracks are soloed or the current track is soloed
            if (!song->track(track)->isMuted() && (!solo || (solo && song->track(track)->isSolo()))) {
                unsigned int volume = 127;
                int delay = 0, repeat = -1, hold = -1;
                unsigned char basenote = block->note(line_, track);
                unsigned char instrument = block->instrument(line_, track);
                unsigned char note = basenote;
                Block *arpeggio = NULL;

                if (note != 0) {
                    // Start the arpeggio from the beginning if a note is played on the track
                    if (tick == 0) {
                        trackStatus->line = 0;
                    }
                } else {
                    basenote = trackStatus->baseNote;
                }

                int arpeggioInstrument = note != 0 && instrument > 0 ? (instrument - 1) : trackStatus->instrument;
                if (arpeggioInstrument >= 0 && trackStatus->line >= 0) {
                    // Add arpeggio note (if any) to the track's base note
                    arpeggio = song->instrument(arpeggioInstrument)->arpeggio();
                    if (arpeggio != NULL) {
                        unsigned char arpeggioNote = arpeggio->note(trackStatus->line, 0);
                        note = arpeggioNote != 0 ? (basenote + ((char)arpeggioNote - (char)song->instrument(arpeggioInstrument)->arpeggioBaseNote())) : 0;
                    }
                }

                // Stop notes if there are new notes about to be played
                if (note != 0) {
                    for (int commandPage = 0; commandPage < commandPages; commandPage++) {
                        unsigned char command = block->command(line_, track, commandPage);
                        unsigned char value = block->commandValue(line_, track, commandPage);

                        if (command != 0 || value != 0) {
                            // Check for previous command if any
                            if (command == CommandPreviousCommandValue) {
                                if (value != 0) {
                                    command = trackStatus->previousCommand;
                                }
                            } else {
                                trackStatus->previousCommand = command;
                            }

                            switch (command) {
                            case CommandRetrigger:
                                delay = (value & 0xf0) >> 4;
                                repeat = value & 0x0f;
                                break;
                            case CommandDelay:
                                delay = value;
                                repeat = -1;
                                break;
                            }
                        }
                    }

                    // Stop currently playing note
                    if (shouldPlayNote(tick, delay, repeat)) {
                        if (trackStatus->note != -1) {
                            midi_->output(trackStatus->midiInterface)->noteOff(trackStatus->midiChannel, trackStatus->note, 127);
                            trackStatus->note = -1;
                        }
                    }
                }

                if (arpeggio != NULL) {
                    // Handle commands on all arpeggio command pages
                    for (int commandPage = 0; commandPage < arpeggio->commandPages(); commandPage++) {
                        unsigned char command = arpeggio->command(trackStatus->line, 0, commandPage);
                        unsigned char value = arpeggio->commandValue(trackStatus->line, 0, commandPage);
                        handleCommand(trackStatus, note, instrument, command, value, &volume, &delay, &repeat, &hold);
                    }
                }

                bool hadVolume = volume > 0;
                // Handle commands on all command pages
                for (int commandPage = 0; commandPage < commandPages; commandPage++) {
                    unsigned char command = block->command(line_, track, commandPage);
                    unsigned char value = block->commandValue(line_, track, commandPage);
                    handleCommand(trackStatus, note, instrument, command, value, &volume, &delay, &repeat, &hold);
                }

                // Set the channel base note and instrument regardless of whether there's an actual note to be played right now
                if (basenote != 0) {
                    trackStatus->baseNote = basenote;
                    if (instrument != 0) {
                        trackStatus->instrument = instrument - 1;
                    }
                }

                // Is there a note to play?
                if (note != 0 && shouldPlayNote(tick, delay, repeat)) {
                    Instrument *instr = NULL;

                    note--;

                    // Use previous instrument if none defined
                    if (instrument == 0) {
                        instrument = trackStatus->instrument + 1;
                    }

                    // Play note if instrument is defined
                    if (instrument != 0) {
                        playNote(instrument - 1, note, volume, track, true);

                        if (instrument <= song->instruments()) {
                            instr = song->instrument(instrument - 1);
                        }
                    }

                    if (instr != NULL) {
                        // If no hold value was defined use the instrument's hold value
                        if (hold == -1) {
                            hold = instr->hold();
                        }

                        trackStatus->hold = hold == 0 ? -1 : hold;

                        // If there would have been volume but the block's commands killed it, stop the arpeggio
                        if (hadVolume && volume == 0) {
                            trackStatus->line = -1;
                        }
                    }
                }

                // First tick, no note but instrument defined?
                if (tick == 0 && note == 0 && instrument > 0 && trackStatus->hold >= 0) {
                    if (song->instrument(instrument - 1)->midiInterface() == trackStatus->midiInterface) {
                        trackStatus->hold += song->instrument(instrument - 1)->hold();
                    }
                }
            }
        }

        // Play notes scheduled to be played
        for (int i = 0; i < postponedNotes.count(); i++) {
            const NoteOn &noteOn = postponedNotes.at(i);
            midi_->output(noteOn.midiInterface)->noteOn(noteOn.midiChannel, noteOn.note, noteOn.volume);
        }
        postponedNotes.clear();

        // Decrement hold times of notes and stop notes that should be stopped
        for (int track = 0; track < song->maxTracks(); track++) {
            QSharedPointer<TrackStatus> trackStatus = trackStatuses[track];
            if (trackStatus->hold >= 0) {
                trackStatus->hold--;
                if (trackStatus->hold < 0 && trackStatus->note != -1) {
                    midi_->output(trackStatus->midiInterface)->noteOff(trackStatus->midiChannel, trackStatus->note, 127);
                    trackStatus->note = -1;
                }
            }
        }

        // Next tick
        ticksSoFar++;
        tick++;
        tick %= song->ticksPerLine();

        // Advance and handle post commands if ticksperline ticks have passed
        if (tick == 0) {
            bool changeBlock = false;

            line_++;

            // Advance arpeggios
            for (int track = 0; track < song->maxTracks(); track++) {
                QSharedPointer<TrackStatus> trackStatus = trackStatuses[track];
                int arpeggioInstrument = trackStatus->instrument;

                if (arpeggioInstrument >= 0 && trackStatus->baseNote >= 0 && trackStatus->line >= 0) {
                    Instrument *instrument = song->instrument(arpeggioInstrument);
                    if (instrument->arpeggio() != NULL) {
                        trackStatus->line++;
                        trackStatus->line %= instrument->arpeggio()->length();
                    }
                }
            }

            switch (postCommand) {
            case CommandEndBlock:
                line_ = postValue;
                if (mode_ == ModePlaySong) {
                    looped = nextPosition();
                    changeBlock = true;
                }
                break;
            case CommandPlayseqPosition:
                line_ = 0;
                position_ = postValue;
                if (position_ >= song->playseq(playseq_)->length()) {
                    position_ = 0;
                    looped = nextSection();
                }
                changeBlock = true;
                break;
            case CommandTempo:
                // COMMAND_TPL and COMMAND_TEMPO can only mean "stop" as stop cmds
                killThread = true;
                break;
            default:
                // Advance in block
                if (line_ >= song->block(block_)->length()) {
                    line_ = 0;
                    if (mode_ == ModePlaySong) {
                        looped = nextPosition();
                        changeBlock = true;
                    }
                }
                break;
            }
            postCommand = 0;
            postValue = 0;

            if (changeBlock) {
                updateLocation();
            }
        }

        // Check whether this thread should be killed
        if (killThread || (killWhenLooped && looped)) {
            break;
        }
        song->unlock();
        mutex.unlock();

        if (line_ != oldLine) {
            emit lineChanged(line_);
        }

        if (scheduler != NULL) {
            struct timeval now;
            gettimeofday(&now, NULL);

            unsigned int time = (unsigned int)(playedSoFar.tv_sec * 1000 + playedSoFar.tv_usec / 1000 + (now.tv_sec * 1000 + now.tv_usec / 1000) - (playingStarted.tv_sec * 1000 + playingStarted.tv_usec / 1000)) / 1000;

            if (time != oldTime) {
                emit timeChanged(time);
                oldTime = time;
            }
        }
    }

    // Calculate how long the song has been playing
    struct timeval now;
    gettimeofday(&now, NULL);
    now.tv_sec -= playingStarted.tv_sec;
    if (now.tv_usec >= playingStarted.tv_usec) {
        now.tv_usec -= playingStarted.tv_usec;
    } else {
        now.tv_usec += 1000000 - playingStarted.tv_usec;
        now.tv_sec--;
    }
    playedSoFar.tv_sec += now.tv_sec;
    playedSoFar.tv_usec += now.tv_usec;
    if (playedSoFar.tv_usec > 1000000) {
        playedSoFar.tv_sec++;
        playedSoFar.tv_usec -= 1000000;
    }

    if (scheduler != NULL) {
        scheduler->stop();
    }

    // Stop all playing notes
    stopNotes();

    // The mutex is locked if the thread was killed and loop broken
    song->unlock();
    mutex.unlock();

    if (line_ != oldLine) {
        emit lineChanged(line_);
    }
}

void Player::playWithoutScheduling()
{
    scheduler = NULL;
    mode_ = ModePlaySong;
    killWhenLooped = true;
    for (int instrument = 0; instrument < song->instruments(); instrument++) {
        song->instrument(instrument)->setMidiInterface(0);
    }
    midi_->output(0)->tempo(song->tempo());
    run();
    stopNotes();
}

void Player::play(Mode mode, bool cont)
{
    stop();

    Mode oldMode = mode_;
    int oldLine = line_;
    mode_ = mode;
    tick = 0;
    ticksSoFar = 0;

    switch (mode) {
    case ModePlaySong:
        if (!cont) {
            section_ = 0;
            position_ = 0;
            line_ = 0;
        }
        updateLocation(true);
        break;
    case ModePlayBlock:
        if (!cont) {
            line_ = 0;
        }
        break;
    default:
        break;
    }

    if (line_ != oldLine) {
        emit lineChanged(line_);
    }

    // Get the starting time
    resetTime(!cont);

    // Send MIDI start or continue if sync is requested
    if (mode != ModeIdle && song->sendSync()) {
        if (cont) {
            midi()->cont();
        } else {
            midi()->start();
        }
    }

    // For some reason the priority setting crashes with realtime Jack
    //            if (editor == NULL || editor_player_get_external_sync(editor) != EXTERNAL_SYNC_JACK_START_ONLY)
    start(syncMode == Off ? QThread::TimeCriticalPriority : QThread::NormalPriority);

    if (mode_ != oldMode) {
        emit modeChanged(mode_);
    }
}

void Player::stop()
{
    if (mode_ != ModeIdle) {
        mode_ = ModeIdle;
        emit modeChanged(mode_);
    }

    if (isRunning()) {
        // Mark the thread for killing
        mutex.lock();
        killThread = true;
        mutex.unlock();

        // If external sync is used send sync to get out of the sync wait loop
        if (syncMode != Off) {
            externalSync(0);
        }

        // Send MIDI stop if sync is requested
        if (song->sendSync()) {
            midi()->stop();
        }

        // Wait until the thread is dead
        wait();
        killThread = false;
    } else {
        stopNotes();
    }
}

void Player::playSong()
{
    play(ModePlaySong, false);
}

void Player::playBlock()
{
    play(ModePlayBlock, false);
}

void Player::continueSong()
{
    play(ModePlaySong, true);
}

void Player::continueBlock()
{
    play(ModePlayBlock, true);
}

void Player::trackStatusCreate(bool recreateAll)
{
    int maxTracks = song != NULL ? song->maxTracks() : 0;

    // Free the extraneous status structures
    while (trackStatuses.count() > (recreateAll ? 0 : maxTracks)) {
        trackStatuses.removeLast();
    }

    // Set new tracks to -1
    for (unsigned int track = trackStatuses.count(); track < maxTracks; track++) {
        trackStatuses.append(QSharedPointer<TrackStatus>(new TrackStatus(track)));
    }
}

void Player::setSong(const QString &path)
{
    if (oldSong != NULL) {
        return;
    }

    stop();

    oldSong = song;
    song = NULL;

    QFile file(path);
    if (file.exists()) {
        file.open(QIODevice::ReadOnly);
        QByteArray header = file.read(4);
        if (header.length() == 4) {
            const char *data = header.data();
            if (data[0] == (ID_MMD2 >> 24) && data[1] == ((ID_MMD2 >> 16) & 0xff) && data[2] == ((ID_MMD2 >> 8) & 0xff) && (data[3] == (ID_MMD0 & 0xff) || data[3] == (ID_MMD1 & 0xff) || data[3] == (ID_MMD2 & 0xff))) {
                song = mmd2ToSong(MMD2_load(path.toUtf8().constData()));
            }
        }
    }

    if (song == NULL) {
        song = new Song(path);
    }

    QTimer::singleShot(0, this, SLOT(init()));
}

void Player::init()
{
    connect(song, SIGNAL(blockLengthChanged()), this, SLOT(resetLine()));
    connect(song, SIGNAL(blocksChanged(int)), this, SLOT(resetBlock()));
    connect(song, SIGNAL(playseqsChanged(int)), this, SLOT(resetPlayseq()));
    connect(song, SIGNAL(sectionsChanged(uint)), this, SLOT(resetSection()));
    connect(song, SIGNAL(trackMutedOrSoloed()), this, SLOT(checkSolo()));

    remapMidiOutputs();

    // Recreate the track status array
    trackStatusCreate(true);
    connect(this->song, SIGNAL(maxTracksChanged(uint)), this, SLOT(trackStatusCreate()));

    // Check solo status
    checkSolo();

    // Send messages to be autosent
    for (int message = 0; message < song->messages(); message++) {
        if (song->message(message)->isAutoSend()) {
            for (int output = 0; output < midi_->outputs(); output++) {
                midi_->output(output)->writeRaw(song->message(message)->data());
            }
        }
    }

    // Reset to the beginning
    block_ = 0;
    section_ = 0;
    playseq_ = 0;
    position_ = 0;
    line_ = 0;

    emit songChanged(song);
    updateLocation(true);
    if (!from_export) {
      delete oldSong;
      oldSong = NULL;
    }
}

void Player::setSection(int section)
{
    mutex.lock();

    unsigned int oldSection = section_;

    // Bounds checking
    if (section < 0) {
        section = 0;
    } else if (section >= song->sections()) {
        section = song->sections() - 1;
    }

    section_ = section;

    mutex.unlock();

    if (section_ != oldSection) {
        emit sectionChanged(section_);
    }
}

void Player::setPlayseq(int playseq)
{
    mutex.lock();

    unsigned int oldPlayseq = playseq_;

    // Bounds checking
    if (playseq < 0) {
        playseq = 0;
    } else if (playseq >= song->playseqs()) {
        playseq = song->playseqs() - 1;
    }

    playseq_ = playseq;

    mutex.unlock();

    if (playseq_ != oldPlayseq) {
        emit playseqChanged(playseq_);
    }

    setPosition(position_);
}

void Player::setPosition(int position)
{
    mutex.lock();

    unsigned int oldPosition = position_;

    // Bounds checking
    if (position < 0) {
        position = 0;
    } else if (position >= song->playseq(playseq_)->length()) {
        position = song->playseq(playseq_)->length() - 1;
    }

    position_ = position;

    mutex.unlock();

    if (position_ != oldPosition) {
        emit positionChanged(position_);
    }
}

void Player::setBlock(int block)
{
    mutex.lock();

    unsigned int oldBlock = block_;

    // Bounds checking
    if (block < 0) {
        block = 0;
    } else if (block >= song->blocks()) {
        block = song->blocks() - 1;
    }

    block_ = block;

    mutex.unlock();

    if (block_ != oldBlock) {
        emit blockChanged(block_);
    }
}

void Player::setLine(int line, bool wrap)
{
    mutex.lock();

    unsigned int oldLine = line_;

    // Bounds checking
    if (wrap) {
        while (line < 0) {
            line += song->block(block_)->length();
        }
        while (line >= song->block(block_)->length()) {
            line -= song->block(block_)->length();
        }
    } else {
        if (line < 0) {
            line = 0;
        } else if (line >= song->block(block_)->length()) {
            line = song->block(block_)->length() - 1;
        }
    }

    line_ = line;

    mutex.unlock();

    if (line_ != oldLine) {
        emit lineChanged(line_);
    }
}

void Player::setTick(int tick)
{
    mutex.lock();
    this->tick = tick;
    mutex.unlock();
}

void Player::resetTime(bool resetSofar)
{
    gettimeofday(&playingStarted, NULL);

    if (resetSofar) {
        playedSoFar.tv_sec = 0;
        playedSoFar.tv_usec = 0;
    }
}

void Player::checkSolo()
{
    int solo = 0;

    for (int track = 0; track < song->maxTracks(); track++) {
        solo |= song->track(track)->isSolo();
    }

    this->solo = solo;
}

void Player::remapMidiOutputs()
{
    for (int instrument = 0; instrument < song->instruments(); instrument++) {
        int output = midi_->output(song->instrument(instrument)->midiInterfaceName());
        if (output >= 0) {
            song->instrument(instrument)->setMidiInterface(output);
        }
    }

    // Recreate the track status array
    trackStatusCreate();

    // Remove extraneous controller values
    while (midi_->outputs() < midiControllerValues.count()) {
        midiControllerValues.removeLast();
    }

    // Create new controller values
    for (int output = midiControllerValues.count(); output < midi_->outputs(); output++) {
        midiControllerValues.append(QVector<unsigned char>(16 * VALUES));
    }
}

void Player::lock()
{
    mutex.lock();
}

void Player::unlock()
{
    mutex.unlock();
}

void Player::externalSync(unsigned int ticks)
{
    mutex.lock();
    if (mode_ != ModeIdle) {
        externalSyncTicks += ticks;
    }
    externalSync_.wakeAll();
    mutex.unlock();
}

void Player::setExternalSync(ExternalSync externalSync)
{
    ExternalSync prevsyncMode = syncMode;

    syncMode = externalSync;

    if (syncMode == Off && prevsyncMode != Off) {
        this->externalSync(0);
    }
}

void Player::setScheduler(Scheduler *scheduler)
{
    this->scheduler = scheduler;
}

void Player::setKillWhenLooped(bool killWhenLooped)
{
    this->killWhenLooped = killWhenLooped;
}

unsigned int Player::section() const
{
    return section_;
}

unsigned int Player::playseq() const
{
    return playseq_;
}

unsigned int Player::position() const
{
    return position_;
}

unsigned int Player::block() const
{
    return block_;
}

unsigned int Player::line() const
{
    return line_;
}

Player::Mode Player::mode() const
{
    return mode_;
}

MIDI *Player::midi() const
{
    return midi_;
}

Player::TrackStatus::TrackStatus(unsigned int track) :
    track(track)
{
    reset();
}

void Player::TrackStatus::reset()
{
    instrument = -1;
    line = -1;
    previousCommand = 0;
    note = -1;
    midiChannel = -1;
    midiInterface = -1;
    volume = -1;
    hold = -1;
}
