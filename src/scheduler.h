/*
 * scheduler.h
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

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <QObject>
#include <QList>

class Song;

class Scheduler : public QObject
{
    Q_OBJECT

public:
    virtual void start(struct timeval &startTime);
    virtual void waitForTick(Song *song, bool schedulerChanged);
    virtual void stop();
    virtual const char *name() const = 0;
    static QList<Scheduler *> schedulers();

protected:
    static QList<Scheduler *> schedulers_;

    explicit Scheduler(QObject *parent = 0);
    virtual ~Scheduler();

    struct timeval next, now;
};

#endif // SCHEDULER_H
