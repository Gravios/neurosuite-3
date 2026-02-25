/***************************************************************************
 *   Copyright (C) 2004-2011 by Michael Zugaro                             *
 *   michael.zugaro@college-de-france.fr                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "progressbar.h"
#include "customtypes.h"
#include <iostream>

void ProgressBar::start()
{
    std::cout << label << " [" << step << "]";
    int n = PROGRESS_MAX_N_CHARS - (int)step.length() + 1;
    if (n > 0) for (int i = 0; i < n; i++) std::cout << " ";
    std::cout << "0% ";
    for (int i = 0; i < length; i++) std::cout << ".";
    std::cout << " 100%\b\b\b\b\b";
    for (int i = 0; i < length; i++) std::cout << "\b";
}

void ProgressBar::advance()
{
    done++;
    if ((float)done / (float)total > (float)marks / (float)length) {
        int nMarksToAdd = (int)((float)done / (float)total * (float)length - (float)marks);
        for (int i = 0; i < nMarksToAdd; i++) std::cout << "#";
        marks += nMarksToAdd;
        std::cout << std::flush;
    }
}

void ProgressBar::message(std::string msg)
{
    for (int i = 0; i < length - marks; i++) std::cout << ".";
    std::cout << " 100%  " << msg << std::endl;

    std::cout << label << " [" << step << "]";
    int n = PROGRESS_MAX_N_CHARS - (int)step.length() + 1;
    if (n > 0) for (int i = 0; i < n; i++) std::cout << " ";
    std::cout << "0% ";
    for (int i = 0; i < marks; i++) std::cout << "#";
    for (int i = 0; i < length - marks; i++) std::cout << ".";
    std::cout << " 100%\b\b\b\b\b";
    for (int i = 0; i < length - marks; i++) std::cout << "\b";
}
