/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "UnitConverter.h"

#include <QSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGridLayout>

#include "base/Debug.h"
#include "base/Pitch.h"
#include "base/Preferences.h"

using namespace std;

static QString pianoNotes[] = {
    "C", "C# / Db", "D", "D# / Eb", "E",
    "F", "F# / Gb", "G", "G# / Ab", "A", "A# / Bb", "B"
};

UnitConverter::UnitConverter(QWidget *parent) :
    QDialog(parent)
{
    QGridLayout *grid = new QGridLayout;
    setLayout(grid);

    m_hz = new QDoubleSpinBox;
    m_hz->setSuffix(QString(" Hz"));
    m_hz->setDecimals(6);
    m_hz->setMinimum(1e-3);
    m_hz->setMaximum(1e6);
    m_hz->setValue(440);
    connect(m_hz, SIGNAL(valueChanged(double)),
	    this, SLOT(hzChanged(double)));

    // The min and max range values for all the remaining controls are
    // determined by the min and max Hz above
    
    m_midi = new QSpinBox;
    m_midi->setMinimum(-156);
    m_midi->setMaximum(203);
    connect(m_midi, SIGNAL(valueChanged(int)),
	    this, SLOT(midiChanged(int)));

    m_note = new QComboBox;
    for (int i = 0; i < 12; ++i) {
	m_note->addItem(pianoNotes[i]);
    }
    connect(m_note, SIGNAL(currentIndexChanged(int)),
	    this, SLOT(noteChanged(int)));

    m_octave = new QSpinBox;
    m_octave->setMinimum(-14);
    m_octave->setMaximum(15);
    connect(m_octave, SIGNAL(valueChanged(int)),
	    this, SLOT(octaveChanged(int)));

    m_cents = new QDoubleSpinBox;
    m_cents->setSuffix(tr(" cents"));
    m_cents->setDecimals(4);
    m_cents->setMinimum(-50);
    m_cents->setMaximum(50);
    connect(m_cents, SIGNAL(valueChanged(double)),
	    this, SLOT(centsChanged(double)));

    m_piano = new QSpinBox;
    //!!!
    
    int row = 1;
    
    grid->addWidget(m_hz, row, 0);
    grid->addWidget(new QLabel(tr("=")), row, 1);

    grid->addWidget(new QLabel(tr("+")), row, 7);
    grid->addWidget(m_cents, row, 8);

    grid->addWidget(new QLabel(tr("Piano note")), row, 2, 1, 2);
    grid->addWidget(m_note, row, 4);
    grid->addWidget(new QLabel(tr("in octave")), row, 5);
    grid->addWidget(m_octave, row, 6);

    ++row;
    
    grid->addWidget(new QLabel(tr("MIDI pitch")), row, 2, 1, 2);
    grid->addWidget(m_midi, row, 4);
    
    ++row;

    grid->addWidget
	(new QLabel(tr("With concert A tuning frequency at %1 Hz, and "
		       "middle C residing in octave %2.\n"
		       "(These can be changed in the application preferences.)")
		    .arg(Preferences::getInstance()->getTuningFrequency())
		    .arg(Preferences::getInstance()->getOctaveOfMiddleC())),
	 row, 0, 1, 9);

    ++row;
    
    grid->addWidget
	(new QLabel(tr("Note that only pitches in the range 0 to 127 are valid "
		       "in the MIDI protocol.")),
	 row, 0, 1, 9);

    ++row;
    
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    grid->addWidget(bb, row, 0, 1, 9);
    connect(bb, SIGNAL(rejected()), this, SLOT(close()));

    updateAllFromHz();
}

UnitConverter::~UnitConverter()
{
}

void
UnitConverter::hzChanged(double hz)
{
    cerr << "hzChanged: " << hz << endl;
    updateAllFromHz();
}

void
UnitConverter::midiChanged(int midi)
{
    cerr << "midiChanged: " << midi << endl;
    double hz = Pitch::getFrequencyForPitch(m_midi->value(), m_cents->value());
    cerr << "hz -> " << hz << endl;
    m_hz->setValue(hz);
}

void
UnitConverter::noteChanged(int note)
{
    cerr << "noteChanged: " << note << endl;
    int pitch = Pitch::getPitchForNoteAndOctave(m_note->currentIndex(),
						m_octave->value());
    double hz = Pitch::getFrequencyForPitch(pitch, m_cents->value());
    cerr << "hz -> " << hz << endl;
    m_hz->setValue(hz);
}

void
UnitConverter::octaveChanged(int oct)
{
    cerr << "octaveChanged: " << oct << endl;
    int pitch = Pitch::getPitchForNoteAndOctave(m_note->currentIndex(),
						m_octave->value());
    double hz = Pitch::getFrequencyForPitch(pitch, m_cents->value());
    cerr << "hz -> " << hz << endl;
    m_hz->setValue(hz);
}

void
UnitConverter::centsChanged(double cents)
{
    cerr << "centsChanged: " << cents << endl;
    double hz = Pitch::getFrequencyForPitch(m_midi->value(), m_cents->value());
    cerr << "hz -> " << hz << endl;
    m_hz->setValue(hz);
}

void
UnitConverter::pianoChanged(int piano)
{
    cerr << "pianoChanged: " << piano << endl;
}

void
UnitConverter::updateAllFromHz()
{
    float cents = 0;
    int pitch = Pitch::getPitchForFrequency(m_hz->value(), &cents);
    int note, octave;
    Pitch::getNoteAndOctaveForPitch(pitch, note, octave);

    cerr << "pitch " << pitch << " note " << note << " octave " << octave << " cents " << cents << endl;
    
    m_midi->blockSignals(true);
    m_cents->blockSignals(true);
    m_note->blockSignals(true);
    m_octave->blockSignals(true);
    
    m_midi->setValue(pitch);
    m_cents->setValue(cents);
    m_note->setCurrentIndex(note);
    m_octave->setValue(octave);
    
    m_midi->blockSignals(false);
    m_cents->blockSignals(false);
    m_note->blockSignals(false);
    m_octave->blockSignals(false);
}



 
