/***************************************************************************
 *   Copyright (C) 2004-2011 by Michael Zugaro                             *
 *   michael.zugaro@college-de-france.fr                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef PROGRESS_BAR
#define PROGRESS_BAR

#include <string>
#include <iosfwd>

/**
 * Simple terminal progress bar.
 * @author Michael Zugaro
 */
class ProgressBar {
public:
    /**
     * @param label  description, e.g. file base name
     * @param step   processing step, e.g. "NEV" or "SYNC"
     * @param total  total number of iterations to reach 100%
     * @param length bar width in characters (default 50)
     */
    ProgressBar(std::string label, std::string step, int total, int length = 50)
        : label(std::move(label)), step(std::move(step)),
          done(0), total(total), marks(0), length(length) {}

    virtual ~ProgressBar() { /* newline emitted by start/advance callers */ }

    virtual void start();
    virtual void advance();
    virtual void message(std::string msg);

private:
    std::string label;
    std::string step;
    int done;
    int total;
    int marks;
    int length;
};

#endif // PROGRESS_BAR
