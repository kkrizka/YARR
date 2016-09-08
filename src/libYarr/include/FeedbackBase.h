/*
 * Authors: T. Heim <timon.heim@cern.ch>,
 * Date: 2014-Nov-24
 */

#ifndef FEEDBACKBASE_H
#define FEEDBACKBASE_H

#include "Histo2d.h"
#include "LoopActionBase.h"

class GlobalFeedbackBase {
    public:
        virtual void feedback(unsigned channel, double sign, bool last) {};
        virtual void feedbackBinary(unsigned channel, double sign, bool last) {}; // TODO Algorithm should be selected in scan
};

class PixelFeedbackBase {
    public:
        virtual void feedback(unsigned channel, Histo2d *h) {};
};

#endif
