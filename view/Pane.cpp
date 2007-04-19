/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "Pane.h"
#include "layer/Layer.h"
#include "data/model/Model.h"
#include "base/ZoomConstraint.h"
#include "base/RealTime.h"
#include "base/Profiler.h"
#include "ViewManager.h"
#include "base/CommandHistory.h"
#include "layer/WaveformLayer.h"

#include <QPaintEvent>
#include <QPainter>
#include <iostream>
#include <cmath>

//!!! for HUD -- pull out into a separate class
#include <QFrame>
#include <QGridLayout>
#include <QPushButton>
#include "widgets/Thumbwheel.h"
#include "widgets/Panner.h"
#include "widgets/RangeInputDialog.h"
#include "widgets/NotifyingPushButton.h"

using std::cerr;
using std::endl;

Pane::Pane(QWidget *w) :
    View(w, true),
    m_identifyFeatures(false),
    m_clickedInRange(false),
    m_shiftPressed(false),
    m_ctrlPressed(false),
    m_navigating(false),
    m_resizing(false),
    m_centreLineVisible(true),
    m_scaleWidth(0),
    m_headsUpDisplay(0),
    m_vpan(0),
    m_hthumb(0),
    m_vthumb(0),
    m_reset(0)
{
    setObjectName("Pane");
    setMouseTracking(true);
    
    updateHeadsUpDisplay();
}

void
Pane::updateHeadsUpDisplay()
{
    Profiler profiler("Pane::updateHeadsUpDisplay", true);

    if (!isVisible()) return;

/*
    int count = 0;
    int currentLevel = 1;
    int level = 1;
    while (true) {
        if (getZoomLevel() == level) currentLevel = count;
        int newLevel = getZoomConstraintBlockSize(level + 1,
                                                  ZoomConstraint::RoundUp);
        if (newLevel == level) break;
        if (newLevel == 131072) break; //!!! just because
        level = newLevel;
        ++count;
    }

    std::cerr << "Have " << count+1 << " zoom levels" << std::endl;
*/

    Layer *layer = 0;
    if (getLayerCount() > 0) layer = getLayer(getLayerCount() - 1);

    if (!m_headsUpDisplay) {

        m_headsUpDisplay = new QFrame(this);

        QGridLayout *layout = new QGridLayout;
        layout->setMargin(0);
        layout->setSpacing(0);
        m_headsUpDisplay->setLayout(layout);
        
        m_hthumb = new Thumbwheel(Qt::Horizontal);
        m_hthumb->setObjectName(tr("Horizontal Zoom"));
        layout->addWidget(m_hthumb, 1, 0, 1, 2);
        m_hthumb->setFixedWidth(70);
        m_hthumb->setFixedHeight(16);
        m_hthumb->setDefaultValue(0);
        m_hthumb->setSpeed(0.6);
        connect(m_hthumb, SIGNAL(valueChanged(int)), this, 
                SLOT(horizontalThumbwheelMoved(int)));
        connect(m_hthumb, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_hthumb, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

        m_vpan = new Panner;
        layout->addWidget(m_vpan, 0, 1);
        m_vpan->setFixedWidth(12);
        m_vpan->setFixedHeight(70);
        m_vpan->setAlpha(80, 130);
        connect(m_vpan, SIGNAL(rectExtentsChanged(float, float, float, float)),
                this, SLOT(verticalPannerMoved(float, float, float, float)));
        connect(m_vpan, SIGNAL(doubleClicked()),
                this, SLOT(editVerticalPannerExtents()));
        connect(m_vpan, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_vpan, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

        m_vthumb = new Thumbwheel(Qt::Vertical);
        m_vthumb->setObjectName(tr("Vertical Zoom"));
        layout->addWidget(m_vthumb, 0, 2);
        m_vthumb->setFixedWidth(16);
        m_vthumb->setFixedHeight(70);
        connect(m_vthumb, SIGNAL(valueChanged(int)), this, 
                SLOT(verticalThumbwheelMoved(int)));
        connect(m_vthumb, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_vthumb, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

        if (layer) {
            RangeMapper *rm = layer->getNewVerticalZoomRangeMapper();
            if (rm) m_vthumb->setRangeMapper(rm);
        }

        m_reset = new NotifyingPushButton;
        m_reset->setFixedHeight(16);
        m_reset->setFixedWidth(16);
        layout->addWidget(m_reset, 1, 2);
        connect(m_reset, SIGNAL(clicked()), m_hthumb, SLOT(resetToDefault()));
        connect(m_reset, SIGNAL(clicked()), m_vthumb, SLOT(resetToDefault()));
        connect(m_reset, SIGNAL(clicked()), m_vpan, SLOT(resetToDefault()));
        connect(m_reset, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
        connect(m_reset, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));
    }

    int count = 0;
    int current = 0;
    int level = 1;

    //!!! pull out into function (presumably in View)
    bool haveConstraint = false;
    for (LayerList::const_iterator i = m_layers.begin(); i != m_layers.end();
         ++i) {
        if ((*i)->getZoomConstraint() && !(*i)->supportsOtherZoomLevels()) {
            haveConstraint = true;
            break;
        }
    }

    if (haveConstraint) {
        while (true) {
            if (getZoomLevel() == level) current = count;
            int newLevel = getZoomConstraintBlockSize(level + 1,
                                                      ZoomConstraint::RoundUp);
            if (newLevel == level) break;
            level = newLevel;
            if (++count == 50) break;
        }
    } else {
        // if we have no particular constraints, we can really spread out
        while (true) {
            if (getZoomLevel() >= level) current = count;
            int step = level / 10;
            int pwr = 0;
            while (step > 0) {
                ++pwr;
                step /= 2;
            }
            step = 1;
            while (pwr > 0) {
                step *= 2;
                --pwr;
            }
//            std::cerr << level << std::endl;
            level += step;
            if (++count == 100 || level > 262144) break;
        }
    }

//    std::cerr << "Have " << count << " zoom levels" << std::endl;

    m_hthumb->setMinimumValue(0);
    m_hthumb->setMaximumValue(count);
    m_hthumb->setValue(count - current);

//    std::cerr << "set value to " << count-current << std::endl;

//    std::cerr << "default value is " << m_hthumb->getDefaultValue() << std::endl;

    if (count != 50 && m_hthumb->getDefaultValue() == 0) {
        m_hthumb->setDefaultValue(count - current);
//        std::cerr << "set default value to " << m_hthumb->getDefaultValue() << std::endl;
    }

    bool haveVThumb = false;

    if (layer) {
        int defaultStep = 0;
        int max = layer->getVerticalZoomSteps(defaultStep);
        if (max == 0) {
            m_vthumb->hide();
        } else {
            haveVThumb = true;
            m_vthumb->show();
            m_vthumb->blockSignals(true);
            m_vthumb->setMinimumValue(0);
            m_vthumb->setMaximumValue(max);
            m_vthumb->setDefaultValue(defaultStep);
            m_vthumb->setValue(layer->getCurrentVerticalZoomStep());
            m_vthumb->blockSignals(false);

//            std::cerr << "Vertical thumbwheel: min 0, max " << max
//                      << ", default " << defaultStep << ", value "
//                      << m_vthumb->getValue() << std::endl;

        }
    }

    updateVerticalPanner();

    if (m_manager && m_manager->getZoomWheelsEnabled() &&
        width() > 120 && height() > 100) {
        if (!m_headsUpDisplay->isVisible()) {
            m_headsUpDisplay->show();
        }
        if (haveVThumb) {
            m_headsUpDisplay->setFixedHeight(m_vthumb->height() + m_hthumb->height());
            m_headsUpDisplay->move(width() - 86, height() - 86);
        } else {
            m_headsUpDisplay->setFixedHeight(m_hthumb->height());
            m_headsUpDisplay->move(width() - 86, height() - 16);
        }
    } else {
        m_headsUpDisplay->hide();
    }
}

void
Pane::updateVerticalPanner()
{
    if (!m_vpan || !m_manager || !m_manager->getZoomWheelsEnabled()) return;

    // In principle we should show or hide the panner on the basis of
    // whether the top layer has adjustable display extents, and we do
    // that below.  However, we have no basis for layout of the panner
    // if the vertical scroll wheel is not also present.  So if we
    // have no vertical scroll wheel, we should remove the panner as
    // well.  Ideally any layer that implements display extents should
    // implement vertical zoom steps as well, but they don't all at
    // the moment.

    Layer *layer = 0;
    if (getLayerCount() > 0) layer = getLayer(getLayerCount() - 1);
    int discard;
    if (layer && layer->getVerticalZoomSteps(discard) == 0) {
        m_vpan->hide();
        return;
    }

    float vmin, vmax, dmin, dmax;
    if (getTopLayerDisplayExtents(vmin, vmax, dmin, dmax) && vmax != vmin) {
        float y0 = (dmin - vmin) / (vmax - vmin);
        float y1 = (dmax - vmin) / (vmax - vmin);
        m_vpan->blockSignals(true);
        m_vpan->setRectExtents(0, 1.0 - y1, 1, y1 - y0);
        m_vpan->blockSignals(false);
        m_vpan->show();
    } else {
        m_vpan->hide();
    }
}

bool
Pane::shouldIlluminateLocalFeatures(const Layer *layer, QPoint &pos) const
{
    QPoint discard;
    bool b0, b1;

    if (layer == getSelectedLayer() &&
	!shouldIlluminateLocalSelection(discard, b0, b1)) {

	pos = m_identifyPoint;
	return m_identifyFeatures;
    }

    return false;
}

bool
Pane::shouldIlluminateLocalSelection(QPoint &pos,
				     bool &closeToLeft,
				     bool &closeToRight) const
{
    if (m_identifyFeatures &&
	m_manager &&
	m_manager->getToolMode() == ViewManager::EditMode &&
	!m_manager->getSelections().empty() &&
	!selectionIsBeingEdited()) {

	Selection s(getSelectionAt(m_identifyPoint.x(),
				   closeToLeft, closeToRight));

	if (!s.isEmpty()) {
	    if (getSelectedLayer() && getSelectedLayer()->isLayerEditable()) {
		
		pos = m_identifyPoint;
		return true;
	    }
	}
    }

    return false;
}

bool
Pane::selectionIsBeingEdited() const
{
    if (!m_editingSelection.isEmpty()) {
	if (m_mousePos != m_clickPos &&
	    getFrameForX(m_mousePos.x()) != getFrameForX(m_clickPos.x())) {
	    return true;
	}
    }
    return false;
}

void
Pane::setCentreLineVisible(bool visible)
{
    m_centreLineVisible = visible;
    update();
}

void
Pane::paintEvent(QPaintEvent *e)
{
//    Profiler profiler("Pane::paintEvent", true);

    QPainter paint;

    QRect r(rect());

    if (e) {
	r = e->rect();
    }
/*
    paint.begin(this);
    paint.setClipRect(r);

    if (hasLightBackground()) {
	paint.setPen(Qt::white);
	paint.setBrush(Qt::white);
    } else {
	paint.setPen(Qt::black);
	paint.setBrush(Qt::black);
    }
    paint.drawRect(r);

    paint.end();
*/
    View::paintEvent(e);

    paint.begin(this);

    if (e) {
	paint.setClipRect(r);
    }

    const Model *waveformModel = 0; // just for reporting purposes
    
    int fontHeight = paint.fontMetrics().height();
    int fontAscent = paint.fontMetrics().ascent();

    if (m_manager &&
        !m_manager->isPlaying() &&
        m_manager->getToolMode() == ViewManager::SelectMode) {

        for (LayerList::iterator vi = m_layers.end(); vi != m_layers.begin(); ) {
            --vi;

            std::vector<QRect> crosshairExtents;

            if ((*vi)->getCrosshairExtents(this, paint, m_identifyPoint,
                                           crosshairExtents)) {
                (*vi)->paintCrosshairs(this, paint, m_identifyPoint);
                break;
            } else if ((*vi)->isLayerOpaque()) {
                break;
            }
        }
    }

    for (LayerList::iterator vi = m_layers.end(); vi != m_layers.begin(); ) {
        --vi;
            
        if (dynamic_cast<WaveformLayer *>(*vi)) {
            waveformModel = (*vi)->getModel();
        }

        if (!m_manager || !m_manager->shouldShowVerticalScale()) {
            m_scaleWidth = 0;
        } else {
            m_scaleWidth = (*vi)->getVerticalScaleWidth(this, paint);
        }

        if (m_scaleWidth > 0 && r.left() < m_scaleWidth) {

//	    Profiler profiler("Pane::paintEvent - painting vertical scale", true);

//	    std::cerr << "Pane::paintEvent: calling paint.save() in vertical scale block" << std::endl;
            paint.save();
            
            paint.setPen(Qt::black);
            paint.setBrush(Qt::white);
            paint.drawRect(0, -1, m_scaleWidth, height()+1);
            
            paint.setBrush(Qt::NoBrush);
            (*vi)->paintVerticalScale
                (this, paint, QRect(0, 0, m_scaleWidth, height()));
            
            paint.restore();
        }
	
        if (m_identifyFeatures) {
            
            QPoint pos = m_identifyPoint;
            QString desc = (*vi)->getFeatureDescription(this, pos);
	    
            if (desc != "") {
                
                paint.save();
                
                int tabStop =
                    paint.fontMetrics().width(tr("Some lengthy prefix:"));
                
                QRect boundingRect = 
                    paint.fontMetrics().boundingRect
                    (rect(),
                     Qt::AlignRight | Qt::AlignTop | Qt::TextExpandTabs,
                     desc, tabStop);

                if (hasLightBackground()) {
                    paint.setPen(Qt::NoPen);
                    paint.setBrush(QColor(250, 250, 250, 200));
                } else {
                    paint.setPen(Qt::NoPen);
                    paint.setBrush(QColor(50, 50, 50, 200));
                }

                int extra = paint.fontMetrics().descent();
                paint.drawRect(width() - boundingRect.width() - 10 - extra,
                               10 - extra,
                               boundingRect.width() + 2 * extra,
                               boundingRect.height() + extra);

                if (hasLightBackground()) {
                    paint.setPen(QColor(150, 20, 0));
                } else {
                    paint.setPen(QColor(255, 150, 100));
                }
		
                QTextOption option;
                option.setWrapMode(QTextOption::NoWrap);
                option.setAlignment(Qt::AlignRight | Qt::AlignTop);
                option.setTabStop(tabStop);
                paint.drawText(QRectF(width() - boundingRect.width() - 10, 10,
                                      boundingRect.width(),
                                      boundingRect.height()),
                               desc,
                               option);

                paint.restore();
            }
        }

        break;
    }
    
    int sampleRate = getModelsSampleRate();
    paint.setBrush(Qt::NoBrush);

    if (m_centreLineVisible &&
        m_manager &&
        m_manager->shouldShowCentreLine()) {

        QColor c = QColor(0, 0, 0);
        if (!hasLightBackground()) {
            c = QColor(240, 240, 240);
        }
        paint.setPen(c);
        int x = width() / 2 + 1;
	paint.drawLine(x, 0, x, height() - 1);
        paint.drawLine(x-1, 1, x+1, 1);
        paint.drawLine(x-2, 0, x+2, 0);
        paint.drawLine(x-1, height() - 2, x+1, height() - 2);
        paint.drawLine(x-2, height() - 1, x+2, height() - 1);

	paint.setPen(QColor(50, 50, 50));

	int y = height() - fontHeight
	    + fontAscent - 6;
	
	LayerList::iterator vi = m_layers.end();
	
	if (vi != m_layers.begin()) {
	    
	    switch ((*--vi)->getPreferredFrameCountPosition()) {
		
	    case Layer::PositionTop:
		y = fontAscent + 6;
		break;
		
	    case Layer::PositionMiddle:
		y = (height() - fontHeight) / 2
		    + fontAscent;
		break;

	    case Layer::PositionBottom:
		// y already set correctly
		break;
	    }
	}

        if (m_manager && m_manager->shouldShowFrameCount()) {

            if (sampleRate) {

                QString text(QString::fromStdString
                             (RealTime::frame2RealTime
                              (m_centreFrame, sampleRate).toText(true)));
                
                int tw = paint.fontMetrics().width(text);
                int x = width()/2 - 4 - tw;
                
                drawVisibleText(paint, x, y, text, OutlinedText);
            }
            
            QString text = QString("%1").arg(m_centreFrame);
            
            int tw = paint.fontMetrics().width(text);
            int x = width()/2 + 4;
            
            drawVisibleText(paint, x, y, text, OutlinedText);
        }

    } else {

	paint.setPen(QColor(50, 50, 50));
    }

    if (waveformModel &&
        m_manager &&
        m_manager->shouldShowDuration() &&
	r.y() + r.height() >= height() - fontHeight - 6) {

        size_t modelRate = waveformModel->getSampleRate();
	size_t playbackRate = m_manager->getPlaybackSampleRate();
        size_t outputRate = m_manager->getOutputSampleRate();
        
	QString srNote = "";

	// Show (R) for waveform models that will be resampled on
	// playback, and (X) for waveform models that will be played
	// at the wrong rate because their rate differs from the
	// current playback rate (which is not necessarily that of the
	// main model).

        if (playbackRate != 0) {
            if (modelRate == playbackRate) {
                if (modelRate != outputRate) srNote = " " + tr("(R)");
            } else {
                srNote = " " + tr("(X)");
            }
        }

	QString desc = tr("%1 / %2Hz%3")
	    .arg(RealTime::frame2RealTime(waveformModel->getEndFrame(),
					  sampleRate)
		 .toText(false).c_str())
	    .arg(modelRate)
	    .arg(srNote);

	if (r.x() < m_scaleWidth + 5 + paint.fontMetrics().width(desc)) {
	    drawVisibleText(paint, m_scaleWidth + 5,
			    height() - fontHeight + fontAscent - 6,
			    desc, OutlinedText);
	}
    }

    if (m_manager &&
        m_manager->shouldShowLayerNames() &&
        r.y() + r.height() >= height() - m_layers.size() * fontHeight - 6) {

	std::vector<QString> texts;
	int maxTextWidth = 0;

	for (LayerList::iterator i = m_layers.begin(); i != m_layers.end(); ++i) {

	    QString text = (*i)->getLayerPresentationName();
	    int tw = paint.fontMetrics().width(text);
            bool reduced = false;
            while (tw > width() / 3 && text.length() > 4) {
                if (!reduced && text.length() > 8) {
                    text = text.left(text.length() - 4);
                } else {
                    text = text.left(text.length() - 2);
                }
                reduced = true;
                tw = paint.fontMetrics().width(text + "...");
            }
            if (reduced) {
                texts.push_back(text + "...");
            } else {
                texts.push_back(text);
            }
	    if (tw > maxTextWidth) maxTextWidth = tw;
	}
    
	int lly = height() - 6;
        int llx = width() - maxTextWidth - 5;

        if (m_manager->getZoomWheelsEnabled()) {
            lly -= 20;
            llx -= 36;
        }

	if (r.x() + r.width() >= llx) {
	    
	    for (int i = 0; i < texts.size(); ++i) {

		if (i == texts.size() - 1) {
		    paint.setPen(Qt::black);
		}
		
		drawVisibleText(paint, llx,
				lly - fontHeight + fontAscent,
				texts[i], OutlinedText);
		
		lly -= fontHeight;
	    }
	}
    }

    if (m_clickedInRange && m_shiftPressed) {
	if (m_manager && (m_manager->getToolMode() == ViewManager::NavigateMode)) {
	    //!!! be nice if this looked a bit more in keeping with the
	    //selection block
	    paint.setPen(Qt::blue);
	    paint.drawRect(m_clickPos.x(), m_clickPos.y(),
			   m_mousePos.x() - m_clickPos.x(),
			   m_mousePos.y() - m_clickPos.y());
	}
    }
    
    if (selectionIsBeingEdited()) {

	int offset = m_mousePos.x() - m_clickPos.x();
	int p0 = getXForFrame(m_editingSelection.getStartFrame()) + offset;
	int p1 = getXForFrame(m_editingSelection.getEndFrame()) + offset;

	if (m_editingSelectionEdge < 0) {
	    p1 = getXForFrame(m_editingSelection.getEndFrame());
	} else if (m_editingSelectionEdge > 0) {
	    p0 = getXForFrame(m_editingSelection.getStartFrame());
	}

	paint.save();
	if (hasLightBackground()) {
	    paint.setPen(QPen(Qt::black, 2));
	} else {
	    paint.setPen(QPen(Qt::white, 2));
	}

	//!!! duplicating display policy with View::drawSelections

	if (m_editingSelectionEdge < 0) {
	    paint.drawLine(p0, 1, p1, 1);
	    paint.drawLine(p0, 0, p0, height());
	    paint.drawLine(p0, height() - 1, p1, height() - 1);
	} else if (m_editingSelectionEdge > 0) {
	    paint.drawLine(p0, 1, p1, 1);
	    paint.drawLine(p1, 0, p1, height());
	    paint.drawLine(p0, height() - 1, p1, height() - 1);
	} else {
	    paint.setBrush(Qt::NoBrush);
	    paint.drawRect(p0, 1, p1 - p0, height() - 2);
	}
	paint.restore();
    }

    paint.end();
}

bool
Pane::render(QPainter &paint, int xorigin, size_t f0, size_t f1)
{
    if (!View::render(paint, xorigin + m_scaleWidth, f0, f1)) {
        return false;
    }

    if (m_scaleWidth > 0) {

        for (LayerList::iterator vi = m_layers.end(); vi != m_layers.begin(); ) {
            --vi;
            
            paint.save();
            
            paint.setPen(Qt::black);
            paint.setBrush(Qt::white);
            paint.drawRect(xorigin, -1, m_scaleWidth, height()+1);
            
            paint.setBrush(Qt::NoBrush);
            (*vi)->paintVerticalScale
                (this, paint, QRect(xorigin, 0, m_scaleWidth, height()));
            
            paint.restore();
            break;
        }
    }

    return true;
}

QImage *
Pane::toNewImage(size_t f0, size_t f1)
{
    size_t x0 = f0 / getZoomLevel();
    size_t x1 = f1 / getZoomLevel();

    QImage *image = new QImage(x1 - x0 + m_scaleWidth,
                               height(), QImage::Format_RGB32);

    int formerScaleWidth = m_scaleWidth;
            
    if (m_manager && m_manager->shouldShowVerticalScale()) {
        for (LayerList::iterator vi = m_layers.end(); vi != m_layers.begin(); ) {
            --vi;
            QPainter paint(image);
            m_scaleWidth = (*vi)->getVerticalScaleWidth(this, paint);
            break;
        }
    } else {
        m_scaleWidth = 0;
    }

    if (m_scaleWidth != formerScaleWidth) {
        delete image;
        image = new QImage(x1 - x0 + m_scaleWidth,
                           height(), QImage::Format_RGB32);
    }        

    QPainter *paint = new QPainter(image);
    if (!render(*paint, 0, f0, f1)) {
        delete paint;
        delete image;
        return 0;
    } else {
        delete paint;
        return image;
    }
}

QSize
Pane::getImageSize(size_t f0, size_t f1)
{
    QSize s = View::getImageSize(f0, f1);
    QImage *image = new QImage(100, 100, QImage::Format_RGB32);
    QPainter paint(image);

    int sw = 0;
    if (m_manager && m_manager->shouldShowVerticalScale()) {
        for (LayerList::iterator vi = m_layers.end(); vi != m_layers.begin(); ) {
            --vi;
            QPainter paint(image);
            sw = (*vi)->getVerticalScaleWidth(this, paint);
            break;
        }
    }
    
    return QSize(sw + s.width(), s.height());
}

size_t
Pane::getFirstVisibleFrame() const
{
    long f0 = getFrameForX(m_scaleWidth);
    size_t f = View::getFirstVisibleFrame();
    if (f0 < 0 || f0 < long(f)) return f;
    return f0;
}

Selection
Pane::getSelectionAt(int x, bool &closeToLeftEdge, bool &closeToRightEdge) const
{
    closeToLeftEdge = closeToRightEdge = false;

    if (!m_manager) return Selection();

    long testFrame = getFrameForX(x - 5);
    if (testFrame < 0) {
	testFrame = getFrameForX(x);
	if (testFrame < 0) return Selection();
    }

    Selection selection = m_manager->getContainingSelection(testFrame, true);
    if (selection.isEmpty()) return selection;

    int lx = getXForFrame(selection.getStartFrame());
    int rx = getXForFrame(selection.getEndFrame());
    
    int fuzz = 2;
    if (x < lx - fuzz || x > rx + fuzz) return Selection();

    int width = rx - lx;
    fuzz = 3;
    if (width < 12) fuzz = width / 4;
    if (fuzz < 1) fuzz = 1;

    if (x < lx + fuzz) closeToLeftEdge = true;
    if (x > rx - fuzz) closeToRightEdge = true;

    return selection;
}

bool
Pane::canTopLayerMoveVertical()
{
    float vmin, vmax, dmin, dmax;
    if (!getTopLayerDisplayExtents(vmin, vmax, dmin, dmax)) return false;
    if (dmin <= vmin && dmax >= vmax) return false;
    return true;
}

bool
Pane::getTopLayerDisplayExtents(float &vmin, float &vmax,
                                float &dmin, float &dmax,
                                QString *unit) 
{
    Layer *layer = 0;
    if (getLayerCount() > 0) layer = getLayer(getLayerCount() - 1);
    if (!layer) return false;
    bool vlog;
    QString vunit;
    bool rv = (layer->getValueExtents(vmin, vmax, vlog, vunit) &&
               layer->getDisplayExtents(dmin, dmax));
    if (unit) *unit = vunit;
    return rv;
}

bool
Pane::setTopLayerDisplayExtents(float dmin, float dmax)
{
    Layer *layer = 0;
    if (getLayerCount() > 0) layer = getLayer(getLayerCount() - 1);
    if (!layer) return false;
    return layer->setDisplayExtents(dmin, dmax);
}

void
Pane::mousePressEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::RightButton) {
        emit contextHelpChanged("");
        emit rightButtonMenuRequested(mapToGlobal(e->pos()));
        return;
    }

    m_clickPos = e->pos();
    m_clickedInRange = true;
    m_editingSelection = Selection();
    m_editingSelectionEdge = 0;
    m_shiftPressed = (e->modifiers() & Qt::ShiftModifier);
    m_ctrlPressed = (e->modifiers() & Qt::ControlModifier);
    m_dragMode = UnresolvedDrag;

    ViewManager::ToolMode mode = ViewManager::NavigateMode;
    if (m_manager) mode = m_manager->getToolMode();

    m_navigating = false;

    if (mode == ViewManager::NavigateMode || (e->buttons() & Qt::MidButton)) {

	if (mode != ViewManager::NavigateMode) {
	    setCursor(Qt::PointingHandCursor);
	}

	m_navigating = true;
	m_dragCentreFrame = m_centreFrame;
        m_dragStartMinValue = 0;
        
        float vmin, vmax, dmin, dmax;
        if (getTopLayerDisplayExtents(vmin, vmax, dmin, dmax)) {
            m_dragStartMinValue = dmin;
        }

    } else if (mode == ViewManager::SelectMode) {

        if (!hasTopLayerTimeXAxis()) return;

	bool closeToLeft = false, closeToRight = false;
	Selection selection = getSelectionAt(e->x(), closeToLeft, closeToRight);

	if ((closeToLeft || closeToRight) && !(closeToLeft && closeToRight)) {

	    m_manager->removeSelection(selection);

	    if (closeToLeft) {
		m_selectionStartFrame = selection.getEndFrame();
	    } else {
		m_selectionStartFrame = selection.getStartFrame();
	    }

	    m_manager->setInProgressSelection(selection, false);
	    m_resizing = true;
	
	} else {

	    int mouseFrame = getFrameForX(e->x());
	    size_t resolution = 1;
	    int snapFrame = mouseFrame;
	
	    Layer *layer = getSelectedLayer();
	    if (layer && !m_shiftPressed) {
		layer->snapToFeatureFrame(this, snapFrame,
					  resolution, Layer::SnapLeft);
	    }
	    
	    if (snapFrame < 0) snapFrame = 0;
	    m_selectionStartFrame = snapFrame;
	    if (m_manager) {
		m_manager->setInProgressSelection(Selection(snapFrame,
							    snapFrame + resolution),
						  !m_ctrlPressed);
	    }

	    m_resizing = false;
	}

	update();

    } else if (mode == ViewManager::DrawMode) {

	Layer *layer = getSelectedLayer();
	if (layer && layer->isLayerEditable()) {
	    layer->drawStart(this, e);
	}

    } else if (mode == ViewManager::EditMode) {

	if (!editSelectionStart(e)) {
	    Layer *layer = getSelectedLayer();
	    if (layer && layer->isLayerEditable()) {
		layer->editStart(this, e);
	    }
	}
    }

    emit paneInteractedWith();
}

void
Pane::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::RightButton) {
        return;
    }

    ViewManager::ToolMode mode = ViewManager::NavigateMode;
    if (m_manager) mode = m_manager->getToolMode();

    if (m_clickedInRange) {
	mouseMoveEvent(e);
    }

    if (m_navigating || mode == ViewManager::NavigateMode) {

	m_navigating = false;

	if (mode != ViewManager::NavigateMode) {
	    // restore cursor
	    toolModeChanged();
	}

	if (m_shiftPressed) {

	    int x0 = std::min(m_clickPos.x(), m_mousePos.x());
	    int x1 = std::max(m_clickPos.x(), m_mousePos.x());

	    int y0 = std::min(m_clickPos.y(), m_mousePos.y());
	    int y1 = std::max(m_clickPos.y(), m_mousePos.y());

            zoomToRegion(x0, y0, x1, y1);
	}

    } else if (mode == ViewManager::SelectMode) {

        if (!hasTopLayerTimeXAxis()) return;

	if (m_manager && m_manager->haveInProgressSelection()) {

	    bool exclusive;
	    Selection selection = m_manager->getInProgressSelection(exclusive);
	    
	    if (selection.getEndFrame() < selection.getStartFrame() + 2) {
		selection = Selection();
	    }
	    
	    m_manager->clearInProgressSelection();
	    
	    if (exclusive) {
		m_manager->setSelection(selection);
	    } else {
		m_manager->addSelection(selection);
	    }
	}
	
	update();

    } else if (mode == ViewManager::DrawMode) {

	Layer *layer = getSelectedLayer();
	if (layer && layer->isLayerEditable()) {
	    layer->drawEnd(this, e);
	    update();
	}

    } else if (mode == ViewManager::EditMode) {

	if (!editSelectionEnd(e)) {
	    Layer *layer = getSelectedLayer();
	    if (layer && layer->isLayerEditable()) {
		layer->editEnd(this, e);
		update();
	    }
	}
    }

    m_clickedInRange = false;

    emit paneInteractedWith();
}

void
Pane::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::RightButton) {
        return;
    }

    updateContextHelp(&e->pos());

    ViewManager::ToolMode mode = ViewManager::NavigateMode;
    if (m_manager) mode = m_manager->getToolMode();

    QPoint prevPoint = m_identifyPoint;
    m_identifyPoint = e->pos();

    if (!m_clickedInRange) {
	
	if (mode == ViewManager::SelectMode && hasTopLayerTimeXAxis()) {
	    bool closeToLeft = false, closeToRight = false;
	    getSelectionAt(e->x(), closeToLeft, closeToRight);
	    if ((closeToLeft || closeToRight) && !(closeToLeft && closeToRight)) {
		setCursor(Qt::SizeHorCursor);
	    } else {
		setCursor(Qt::ArrowCursor);
	    }
	}

        if (!m_manager->isPlaying()) {

            if (getSelectedLayer()) {

                bool previouslyIdentifying = m_identifyFeatures;
                m_identifyFeatures = true;
                
                if (m_identifyFeatures != previouslyIdentifying ||
                    m_identifyPoint != prevPoint) {
                    update();
                }
            }
        }

	return;
    }

    if (m_navigating || mode == ViewManager::NavigateMode) {

	if (m_shiftPressed) {

	    m_mousePos = e->pos();
	    update();

	} else {

            dragTopLayer(e);
        }

    } else if (mode == ViewManager::SelectMode) {

        if (!hasTopLayerTimeXAxis()) return;

        dragExtendSelection(e);

    } else if (mode == ViewManager::DrawMode) {

	Layer *layer = getSelectedLayer();
	if (layer && layer->isLayerEditable()) {
	    layer->drawDrag(this, e);
	}

    } else if (mode == ViewManager::EditMode) {

	if (!editSelectionDrag(e)) {
	    Layer *layer = getSelectedLayer();
	    if (layer && layer->isLayerEditable()) {
		layer->editDrag(this, e);
	    }
	}
    }
}

void
Pane::zoomToRegion(int x0, int y0, int x1, int y1)
{
    int w = x1 - x0;
	    
    long newStartFrame = getFrameForX(x0);
	    
    long visibleFrames = getEndFrame() - getStartFrame();
    if (newStartFrame <= -visibleFrames) {
        newStartFrame  = -visibleFrames + 1;
    }
	    
    if (newStartFrame >= long(getModelsEndFrame())) {
        newStartFrame  = getModelsEndFrame() - 1;
    }
	    
    float ratio = float(w) / float(width());
//	std::cerr << "ratio: " << ratio << std::endl;
    size_t newZoomLevel = (size_t)nearbyint(m_zoomLevel * ratio);
    if (newZoomLevel < 1) newZoomLevel = 1;

//	std::cerr << "start: " << m_startFrame << ", level " << m_zoomLevel << std::endl;
    setZoomLevel(getZoomConstraintBlockSize(newZoomLevel));
    setStartFrame(newStartFrame);

    QString unit;
    float min, max;
    bool log;
    Layer *layer = 0;
    for (LayerList::const_iterator i = m_layers.begin();
         i != m_layers.end(); ++i) { 
        if ((*i)->getValueExtents(min, max, log, unit) &&
            (*i)->getDisplayExtents(min, max)) {
            layer = *i;
            break;
        }
    }
            
    if (layer) {
        if (log) {
            min = (min < 0.0) ? -log10f(-min) : (min == 0.0) ? 0.0 : log10f(min);
            max = (max < 0.0) ? -log10f(-max) : (max == 0.0) ? 0.0 : log10f(max);
        }
        float rmin = min + ((max - min) * (height() - y1)) / height();
        float rmax = min + ((max - min) * (height() - y0)) / height();
        std::cerr << "min: " << min << ", max: " << max << ", y0: " << y0 << ", y1: " << y1 << ", h: " << height() << ", rmin: " << rmin << ", rmax: " << rmax << std::endl;
        if (log) {
            rmin = powf(10, rmin);
            rmax = powf(10, rmax);
        }
        std::cerr << "finally: rmin: " << rmin << ", rmax: " << rmax << " " << unit.toStdString() << std::endl;

        layer->setDisplayExtents(rmin, rmax);
        updateVerticalPanner();
    }
}

void
Pane::dragTopLayer(QMouseEvent *e)
{
    // We need to avoid making it too easy to drag both
    // horizontally and vertically, in the case where the
    // mouse is moved "mostly" in horizontal or vertical axis
    // with only a small variation in the other axis.  This is
    // particularly important during playback (when we want to
    // avoid small horizontal motions) or in slow refresh
    // layers like spectrogram (when we want to avoid small
    // vertical motions).
    // 
    // To this end we have horizontal and vertical thresholds
    // and a series of states: unresolved, horizontally or
    // vertically constrained, free.
    //
    // When the mouse first moves, we're unresolved: we
    // restrict ourselves to whichever direction seems safest,
    // until the mouse has passed a small threshold distance
    // from the click point.  Then we lock in to one of the
    // constrained modes, based on which axis that distance
    // was measured in first.  Finally, if it turns out we've
    // also moved more than a certain larger distance in the
    // other direction as well, we may switch into free mode.
    // 
    // If the top layer is incapable of being dragged
    // vertically, the logic is short circuited.

    int xdiff = e->x() - m_clickPos.x();
    int ydiff = e->y() - m_clickPos.y();
    int smallThreshold = 10, bigThreshold = 50;

    bool canMoveVertical = canTopLayerMoveVertical();
    bool canMoveHorizontal = true;

    if (!canMoveHorizontal) {
        m_dragMode = HorizontalDrag;
    }

    if (m_dragMode == UnresolvedDrag) {

        if (abs(ydiff) > smallThreshold &&
            abs(ydiff) > abs(xdiff) * 2) {
            m_dragMode = VerticalDrag;
        } else if (abs(xdiff) > smallThreshold &&
                   abs(xdiff) > abs(ydiff) * 2) {
            m_dragMode = HorizontalDrag;
        } else if (abs(xdiff) > smallThreshold &&
                   abs(ydiff) > smallThreshold) {
            m_dragMode = FreeDrag;
        } else {
            // When playing, we don't want to disturb the play
            // position too easily; when not playing, we don't
            // want to move up/down too easily
            if (m_manager && m_manager->isPlaying()) {
                canMoveHorizontal = false;
            } else {
                canMoveVertical = false;
            }
        }
    }

    if (m_dragMode == VerticalDrag) {
        if (abs(xdiff) > bigThreshold) m_dragMode = FreeDrag;
        else canMoveHorizontal = false;
    }

    if (m_dragMode == HorizontalDrag && canMoveVertical) {
        if (abs(ydiff) > bigThreshold) m_dragMode = FreeDrag;
        else canMoveVertical = false;
    }

    if (canMoveHorizontal) {

        long frameOff = getFrameForX(e->x()) - getFrameForX(m_clickPos.x());

        size_t newCentreFrame = m_dragCentreFrame;
	    
        if (frameOff < 0) {
            newCentreFrame -= frameOff;
        } else if (newCentreFrame >= size_t(frameOff)) {
            newCentreFrame -= frameOff;
        } else {
            newCentreFrame = 0;
        }
	    
        if (newCentreFrame >= getModelsEndFrame()) {
            newCentreFrame = getModelsEndFrame();
            if (newCentreFrame > 0) --newCentreFrame;
        }
                
        if (getXForFrame(m_centreFrame) != getXForFrame(newCentreFrame)) {
            setCentreFrame(newCentreFrame);
        }
    }

    if (canMoveVertical) {

        float vmin = 0.f, vmax = 0.f;
        float dmin = 0.f, dmax = 0.f;

        if (getTopLayerDisplayExtents(vmin, vmax, dmin, dmax)) {

            std::cerr << "ydiff = " << ydiff << std::endl;

            float perpix = (dmax - dmin) / height();
            float valdiff = ydiff * perpix;
            std::cerr << "valdiff = " << valdiff << std::endl;

            float newmin = m_dragStartMinValue + valdiff;
            float newmax = m_dragStartMinValue + (dmax - dmin) + valdiff;
            if (newmin < vmin) {
                newmax += vmin - newmin;
                newmin += vmin - newmin;
            }
            if (newmax > vmax) {
                newmin -= newmax - vmax;
                newmax -= newmax - vmax;
            }
            std::cerr << "(" << dmin << ", " << dmax << ") -> ("
                      << newmin << ", " << newmax << ") (drag start " << m_dragStartMinValue << ")" << std::endl;

            setTopLayerDisplayExtents(newmin, newmax);
            updateVerticalPanner();
        }
    }
}

void
Pane::dragExtendSelection(QMouseEvent *e)
{
    int mouseFrame = getFrameForX(e->x());
    size_t resolution = 1;
    int snapFrameLeft = mouseFrame;
    int snapFrameRight = mouseFrame;
	
    Layer *layer = getSelectedLayer();
    if (layer && !m_shiftPressed) {
        layer->snapToFeatureFrame(this, snapFrameLeft,
                                  resolution, Layer::SnapLeft);
        layer->snapToFeatureFrame(this, snapFrameRight,
                                  resolution, Layer::SnapRight);
    }
	
//	std::cerr << "snap: frame = " << mouseFrame << ", start frame = " << m_selectionStartFrame << ", left = " << snapFrameLeft << ", right = " << snapFrameRight << std::endl;

    if (snapFrameLeft < 0) snapFrameLeft = 0;
    if (snapFrameRight < 0) snapFrameRight = 0;
	
    size_t min, max;
	
    if (m_selectionStartFrame > snapFrameLeft) {
        min = snapFrameLeft;
        max = m_selectionStartFrame;
    } else if (snapFrameRight > m_selectionStartFrame) {
        min = m_selectionStartFrame;
        max = snapFrameRight;
    } else {
        min = snapFrameLeft;
        max = snapFrameRight;
    }

    if (m_manager) {
        m_manager->setInProgressSelection(Selection(min, max),
                                          !m_resizing && !m_ctrlPressed);
    }

    bool doScroll = false;
    if (!m_manager) doScroll = true;
    if (!m_manager->isPlaying()) doScroll = true;
    if (m_followPlay != PlaybackScrollContinuous) doScroll = true;

    if (doScroll) {
        int offset = mouseFrame - getStartFrame();
        int available = getEndFrame() - getStartFrame();
        if (offset >= available * 0.95) {
            int move = int(offset - available * 0.95) + 1;
            setCentreFrame(m_centreFrame + move);
        } else if (offset <= available * 0.10) {
            int move = int(available * 0.10 - offset) + 1;
            if (m_centreFrame > move) {
                setCentreFrame(m_centreFrame - move);
            } else {
                setCentreFrame(0);
            }
        }
    }

    update();
}

void
Pane::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::RightButton) {
        return;
    }

//    std::cerr << "mouseDoubleClickEvent" << std::endl;

    m_clickPos = e->pos();
    m_clickedInRange = true;
    m_shiftPressed = (e->modifiers() & Qt::ShiftModifier);
    m_ctrlPressed = (e->modifiers() & Qt::ControlModifier);

    ViewManager::ToolMode mode = ViewManager::NavigateMode;
    if (m_manager) mode = m_manager->getToolMode();

    if (mode == ViewManager::NavigateMode ||
        mode == ViewManager::EditMode) {

	Layer *layer = getSelectedLayer();
	if (layer && layer->isLayerEditable()) {
	    layer->editOpen(this, e);
	}
    }
}

void
Pane::leaveEvent(QEvent *)
{
    bool previouslyIdentifying = m_identifyFeatures;
    m_identifyFeatures = false;
    if (previouslyIdentifying) update();
    emit contextHelpChanged("");
}

void
Pane::resizeEvent(QResizeEvent *)
{
    updateHeadsUpDisplay();
}

void
Pane::wheelEvent(QWheelEvent *e)
{
    //std::cerr << "wheelEvent, delta " << e->delta() << std::endl;

    int count = e->delta();

    if (count > 0) {
	if (count >= 120) count /= 120;
	else count = 1;
    } 

    if (count < 0) {
	if (count <= -120) count /= 120;
	else count = -1;
    }

    if (e->modifiers() & Qt::ControlModifier) {

	// Scroll left or right, rapidly

	if (getStartFrame() < 0 && 
	    getEndFrame() >= getModelsEndFrame()) return;

	long delta = ((width() / 2) * count * m_zoomLevel);

	if (int(m_centreFrame) < delta) {
	    setCentreFrame(0);
	} else if (int(m_centreFrame) - delta >= int(getModelsEndFrame())) {
	    setCentreFrame(getModelsEndFrame());
	} else {
	    setCentreFrame(m_centreFrame - delta);
	}

    } else {

	// Zoom in or out

	int newZoomLevel = m_zoomLevel;
  
	while (count > 0) {
	    if (newZoomLevel <= 2) {
		newZoomLevel = 1;
		break;
	    }
	    newZoomLevel = getZoomConstraintBlockSize(newZoomLevel - 1, 
						      ZoomConstraint::RoundDown);
	    --count;
	}
	
	while (count < 0) {
	    newZoomLevel = getZoomConstraintBlockSize(newZoomLevel + 1,
						      ZoomConstraint::RoundUp);
	    ++count;
	}
	
	if (newZoomLevel != m_zoomLevel) {
	    setZoomLevel(newZoomLevel);
	}
    }

    emit paneInteractedWith();
}

void
Pane::horizontalThumbwheelMoved(int value)
{
    //!!! dupe with updateHeadsUpDisplay

    int count = 0;
    int level = 1;


    //!!! pull out into function (presumably in View)
    bool haveConstraint = false;
    for (LayerList::const_iterator i = m_layers.begin(); i != m_layers.end();
         ++i) {
        if ((*i)->getZoomConstraint() && !(*i)->supportsOtherZoomLevels()) {
            haveConstraint = true;
            break;
        }
    }

    if (haveConstraint) {
        while (true) {
            if (m_hthumb->getMaximumValue() - value == count) break;
            int newLevel = getZoomConstraintBlockSize(level + 1,
                                                      ZoomConstraint::RoundUp);
            if (newLevel == level) break;
            level = newLevel;
            if (++count == 50) break;
        }
    } else {
        while (true) {
            if (m_hthumb->getMaximumValue() - value == count) break;
            int step = level / 10;
            int pwr = 0;
            while (step > 0) {
                ++pwr;
                step /= 2;
            }
            step = 1;
            while (pwr > 0) {
                step *= 2;
                --pwr;
            }
//            std::cerr << level << std::endl;
            level += step;
            if (++count == 100 || level > 262144) break;
        }
    }
        
    std::cerr << "new level is " << level << std::endl;
    setZoomLevel(level);
}    

void
Pane::verticalThumbwheelMoved(int value)
{
    Layer *layer = 0;
    if (getLayerCount() > 0) layer = getLayer(getLayerCount() - 1);
    if (layer) {
        int defaultStep = 0;
        int max = layer->getVerticalZoomSteps(defaultStep);
        if (max == 0) {
            updateHeadsUpDisplay();
            return;
        }
        if (value > max) {
            value = max;
        }
        layer->setVerticalZoomStep(value);
        updateVerticalPanner();
    }
}    

void
Pane::verticalPannerMoved(float x0, float y0, float w, float h)
{
    float vmin, vmax, dmin, dmax;
    if (!getTopLayerDisplayExtents(vmin, vmax, dmin, dmax)) return;
    float y1 = y0 + h;
    float newmax = vmin + ((1.0 - y0) * (vmax - vmin));
    float newmin = vmin + ((1.0 - y1) * (vmax - vmin));
    std::cerr << "verticalPannerMoved: (" << x0 << "," << y0 << "," << w
              << "," << h << ") -> (" << newmin << "," << newmax << ")" << std::endl;
    setTopLayerDisplayExtents(newmin, newmax);
}

void
Pane::editVerticalPannerExtents()
{
    if (!m_vpan || !m_manager || !m_manager->getZoomWheelsEnabled()) return;

    float vmin, vmax, dmin, dmax;
    QString unit;
    if (!getTopLayerDisplayExtents(vmin, vmax, dmin, dmax, &unit)
        || vmax == vmin) {
        return;
    }

    RangeInputDialog dialog(tr("Enter new range"),
                            tr("New vertical display range, from %1 to %2 %4:")
                            .arg(vmin).arg(vmax).arg(unit),
                            unit, vmin, vmax, this);
    dialog.setRange(dmin, dmax);

    if (dialog.exec() == QDialog::Accepted) {
        dialog.getRange(dmin, dmax);
        setTopLayerDisplayExtents(dmin, dmax);
        updateVerticalPanner();
    }
}

bool
Pane::editSelectionStart(QMouseEvent *e)
{
    if (!m_identifyFeatures ||
	!m_manager ||
	m_manager->getToolMode() != ViewManager::EditMode) {
	return false;
    }

    bool closeToLeft, closeToRight;
    Selection s(getSelectionAt(e->x(), closeToLeft, closeToRight));
    if (s.isEmpty()) return false;
    m_editingSelection = s;
    m_editingSelectionEdge = (closeToLeft ? -1 : closeToRight ? 1 : 0);
    m_mousePos = e->pos();
    return true;
}

bool
Pane::editSelectionDrag(QMouseEvent *e)
{
    if (m_editingSelection.isEmpty()) return false;
    m_mousePos = e->pos();
    update();
    return true;
}

bool
Pane::editSelectionEnd(QMouseEvent *e)
{
    if (m_editingSelection.isEmpty()) return false;

    int offset = m_mousePos.x() - m_clickPos.x();
    Layer *layer = getSelectedLayer();

    if (offset == 0 || !layer) {
	m_editingSelection = Selection();
	return true;
    }

    int p0 = getXForFrame(m_editingSelection.getStartFrame()) + offset;
    int p1 = getXForFrame(m_editingSelection.getEndFrame()) + offset;

    long f0 = getFrameForX(p0);
    long f1 = getFrameForX(p1);

    Selection newSelection(f0, f1);
    
    if (m_editingSelectionEdge == 0) {
	
        CommandHistory::getInstance()->startCompoundOperation
            (tr("Drag Selection"), true);

	layer->moveSelection(m_editingSelection, f0);
	
    } else {
	
        CommandHistory::getInstance()->startCompoundOperation
            (tr("Resize Selection"), true);

	if (m_editingSelectionEdge < 0) {
	    f1 = m_editingSelection.getEndFrame();
	} else {
	    f0 = m_editingSelection.getStartFrame();
	}

	newSelection = Selection(f0, f1);
	layer->resizeSelection(m_editingSelection, newSelection);
    }
    
    m_manager->removeSelection(m_editingSelection);
    m_manager->addSelection(newSelection);

    CommandHistory::getInstance()->endCompoundOperation();

    m_editingSelection = Selection();
    return true;
}

void
Pane::toolModeChanged()
{
    ViewManager::ToolMode mode = m_manager->getToolMode();
//    std::cerr << "Pane::toolModeChanged(" << mode << ")" << std::endl;

    switch (mode) {

    case ViewManager::NavigateMode:
	setCursor(Qt::PointingHandCursor);
	break;
	
    case ViewManager::SelectMode:
	setCursor(Qt::ArrowCursor);
	break;
	
    case ViewManager::EditMode:
	setCursor(Qt::UpArrowCursor);
	break;
	
    case ViewManager::DrawMode:
	setCursor(Qt::CrossCursor);
	break;
/*	
    case ViewManager::TextMode:
	setCursor(Qt::IBeamCursor);
	break;
*/
    }
}

void
Pane::zoomWheelsEnabledChanged()
{
    updateHeadsUpDisplay();
    update();
}

void
Pane::viewZoomLevelChanged(View *v, unsigned long z, bool locked)
{
//    std::cerr << "Pane[" << this << "]::zoomLevelChanged (global now "
//              << (m_manager ? m_manager->getGlobalZoom() : 0) << ")" << std::endl;

    View::viewZoomLevelChanged(v, z, locked);

    if (m_hthumb && !m_hthumb->isVisible()) return;

    if (v != this) {
        if (!locked || !m_followZoom) return;
    }

    if (m_manager && m_manager->getZoomWheelsEnabled()) {
        updateHeadsUpDisplay();
    }
}

void
Pane::propertyContainerSelected(View *v, PropertyContainer *pc)
{
    Layer *layer = 0;

    if (getLayerCount() > 0) {
        layer = getLayer(getLayerCount() - 1);
        disconnect(layer, SIGNAL(verticalZoomChanged()),
                   this, SLOT(verticalZoomChanged()));
    }

    View::propertyContainerSelected(v, pc);
    updateHeadsUpDisplay();

    if (m_vthumb) {
        RangeMapper *rm = 0;
        if (layer) rm = layer->getNewVerticalZoomRangeMapper();
        if (rm) m_vthumb->setRangeMapper(rm);
    }

    if (getLayerCount() > 0) {
        layer = getLayer(getLayerCount() - 1);
        connect(layer, SIGNAL(verticalZoomChanged()),
                this, SLOT(verticalZoomChanged()));
    }
}

void
Pane::verticalZoomChanged()
{
    Layer *layer = 0;

    if (getLayerCount() > 0) {

        layer = getLayer(getLayerCount() - 1);

        if (m_vthumb && m_vthumb->isVisible()) {
            m_vthumb->setValue(layer->getCurrentVerticalZoomStep());
        }
    }
}

void
Pane::updateContextHelp(const QPoint *pos)
{
    QString help = "";

    if (m_clickedInRange) {
        emit contextHelpChanged("");
        return;
    }

    ViewManager::ToolMode mode = ViewManager::NavigateMode;
    if (m_manager) mode = m_manager->getToolMode();

    bool editable = false;
    Layer *layer = getSelectedLayer();
    if (layer && layer->isLayerEditable()) {
        editable = true;
    }
        
    if (mode == ViewManager::NavigateMode) {

        help = tr("Click and drag to navigate");
        
    } else if (mode == ViewManager::SelectMode) {

        if (!hasTopLayerTimeXAxis()) return;

        bool haveSelection = (m_manager && !m_manager->getSelections().empty());

        if (haveSelection) {
            if (editable) {
                help = tr("Click and drag to select a range; hold Shift to avoid snapping to items; hold Ctrl for multi-select; middle-click and drag to navigate");
            } else {
                help = tr("Click and drag to select a range; hold Ctrl for multi-select; middle-click and drag to navigate");
            }                

            if (pos) {
                bool closeToLeft = false, closeToRight = false;
                Selection selection = getSelectionAt(pos->x(), closeToLeft, closeToRight);
                if ((closeToLeft || closeToRight) && !(closeToLeft && closeToRight)) {
                    
                    help = tr("Click and drag to move the selection boundary");
                }
            }
        } else {
            if (editable) {
                help = tr("Click and drag to select a range; hold Shift to avoid snapping to items; middle-click to navigate");
            } else {
                help = tr("Click and drag to select a range; middle-click and drag to navigate");
            }
        }

    } else if (mode == ViewManager::DrawMode) {
        
        //!!! could call through to a layer function to find out exact meaning
	if (editable) {
            help = tr("Click to add a new item in the active layer");
        }
        
    } else if (mode == ViewManager::EditMode) {
        
        //!!! could call through to layer
	if (editable) {
            help = tr("Click and drag an item in the active layer to move it");
            if (pos) {
                bool closeToLeft = false, closeToRight = false;
                Selection selection = getSelectionAt(pos->x(), closeToLeft, closeToRight);
                if (!selection.isEmpty()) {
                    help = tr("Click and drag to move all items in the selected range");
                }
            }
        }
    }

    emit contextHelpChanged(help);
}

void
Pane::mouseEnteredWidget()
{
    QWidget *w = dynamic_cast<QWidget *>(sender());
    if (!w) return;

    if (w == m_vpan) {
        emit contextHelpChanged(tr("Click and drag to adjust the visible range of the vertical scale"));
    } else if (w == m_vthumb) {
        emit contextHelpChanged(tr("Click and drag to adjust the vertical zoom level"));
    } else if (w == m_hthumb) {
        emit contextHelpChanged(tr("Click and drag to adjust the horizontal zoom level"));
    } else if (w == m_reset) {
        emit contextHelpChanged(tr("Reset horizontal and vertical zoom levels to their defaults"));
    }
}

void
Pane::mouseLeftWidget()
{
    emit contextHelpChanged("");
}

QString
Pane::toXmlString(QString indent, QString extraAttributes) const
{
    return View::toXmlString
	(indent,
	 QString("type=\"pane\" centreLineVisible=\"%1\" height=\"%2\" %3")
	 .arg(m_centreLineVisible).arg(height()).arg(extraAttributes));
}


