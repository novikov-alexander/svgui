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

#include "Layer.h"
#include "view/View.h"
#include "data/model/Model.h"
#include "base/CommandHistory.h"

#include <iostream>

#include <QMutexLocker>
#include <QMouseEvent>
#include <QTextStream>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QDomAttr>

#include "LayerFactory.h"
#include "base/PlayParameterRepository.h"

#include <cmath>

Layer::Layer() :
    m_haveDraggingRect(false),
    m_haveCurrentMeasureRect(false)
{
}

Layer::~Layer()
{
//    std::cerr << "Layer::~Layer(" << this << ")" << std::endl;
}

void
Layer::connectSignals(const Model *model)
{
    connect(model, SIGNAL(modelChanged()),
            this, SIGNAL(modelChanged()));

    connect(model, SIGNAL(modelChanged(size_t, size_t)),
	    this, SIGNAL(modelChanged(size_t, size_t)));

    connect(model, SIGNAL(completionChanged()),
	    this, SIGNAL(modelCompletionChanged()));

    connect(model, SIGNAL(alignmentCompletionChanged()),
            this, SIGNAL(modelAlignmentCompletionChanged()));
}

QString
Layer::getPropertyContainerIconName() const
{
    return LayerFactory::getInstance()->getLayerIconName
	(LayerFactory::getInstance()->getLayerType(this));
}

QString
Layer::getLayerPresentationName() const
{
//    QString layerName = objectName();

    LayerFactory *factory = LayerFactory::getInstance();
    QString layerName = factory->getLayerPresentationName
        (factory->getLayerType(this));

    QString modelName;
    if (getModel()) modelName = getModel()->objectName();
	    
    QString text;
    if (modelName != "") {
	text = QString("%1: %2").arg(modelName).arg(layerName);
    } else {
	text = layerName;
    }
	
    return text;
}

void
Layer::setObjectName(const QString &name)
{
    QObject::setObjectName(name);
    emit layerNameChanged();
}

PlayParameters *
Layer::getPlayParameters() 
{
//    std::cerr << "Layer (" << this << ", " << objectName().toStdString() << ")::getPlayParameters: model is "<< getModel() << std::endl;
    const Model *model = getModel();
    if (model) {
	return PlayParameterRepository::getInstance()->getPlayParameters(model);
    }
    return 0;
}

void
Layer::setLayerDormant(const View *v, bool dormant)
{
    const void *vv = (const void *)v;
    QMutexLocker locker(&m_dormancyMutex);
    m_dormancy[vv] = dormant;
}

bool
Layer::isLayerDormant(const View *v) const
{
    const void *vv = (const void *)v;
    QMutexLocker locker(&m_dormancyMutex);
    if (m_dormancy.find(vv) == m_dormancy.end()) return false;
    return m_dormancy.find(vv)->second;
}

void
Layer::showLayer(View *view, bool show)
{
    setLayerDormant(view, !show);
    emit layerParametersChanged();
}

bool
Layer::getXScaleValue(const View *v, int x, float &value, QString &unit) const
{
    if (!hasTimeXAxis()) return false;

    const Model *m = getModel();
    if (!m) return false;

    value = float(v->getFrameForX(x)) / m->getSampleRate();
    unit = "s";
    return true;
}

bool
Layer::getYScaleDifference(const View *v, int y0, int y1,
                           float &diff, QString &unit) const
{
    float v0, v1;
    if (!getYScaleValue(v, y0, v0, unit) ||
        !getYScaleValue(v, y1, v1, unit)) {
        diff = 0.f;
        return false;
    }
    diff = fabsf(v1 - v0);
    return true;
}

bool
Layer::MeasureRect::operator<(const MeasureRect &mr) const
{
    if (haveFrames) {
        if (startFrame == mr.startFrame) {
            if (endFrame != mr.endFrame) {
                return endFrame < mr.endFrame;
            }
        } else {
            return startFrame < mr.startFrame;
        }
    } else {
        if (pixrect.x() == mr.pixrect.x()) {
            if (pixrect.width() != mr.pixrect.width()) {
                return pixrect.width() < mr.pixrect.width();
            }
        } else {
            return pixrect.x() < mr.pixrect.x();
        }
    }

    // the two rects are equal in x and width

    if (pixrect.y() == mr.pixrect.y()) {
        return pixrect.height() < mr.pixrect.height();
    } else {
        return pixrect.y() < mr.pixrect.y();
    }
}

void
Layer::MeasureRect::toXml(QTextStream &stream, QString indent) const
{
    stream << indent;
    stream << QString("<measurement ");

    if (haveFrames) {
        stream << QString("startFrame=\"%1\" endFrame=\"%2\" ")
            .arg(startFrame).arg(endFrame);
    } else {
        stream << QString("startX=\"%1\" endX=\"%2\" ")
            .arg(pixrect.x()).arg(pixrect.x() << pixrect.width());
    }

    stream << QString("startY=\"%1\" endY=\"%2\"/>\n")
        .arg(startY).arg(endY);
}

void
Layer::addMeasurementRect(const QXmlAttributes &attributes)
{
    MeasureRect rect;
    QString fs = attributes.value("startFrame");
    int x0 = 0, x1 = 0;
    if (fs != "") {
        rect.startFrame = fs.toLong();
        rect.endFrame = attributes.value("endFrame").toLong();
        rect.haveFrames = true;
    } else {
        x0 = attributes.value("startX").toInt();
        x1 = attributes.value("endX").toInt();
        rect.haveFrames = false;
    }
    rect.startY = attributes.value("startY").toDouble();
    rect.endY = attributes.value("endY").toDouble();
    rect.pixrect = QRect(x0, 0, x1 - x0, 0);
    addMeasureRectToSet(rect);
}

QString
Layer::AddMeasurementRectCommand::getName() const
{
    return tr("Make Measurement");
}

void
Layer::AddMeasurementRectCommand::execute()
{
    m_layer->addMeasureRectToSet(m_rect);
}

void
Layer::AddMeasurementRectCommand::unexecute()
{
    m_layer->deleteMeasureRectFromSet(m_rect);
}

QString
Layer::DeleteMeasurementRectCommand::getName() const
{
    return tr("Delete Measurement");
}

void
Layer::DeleteMeasurementRectCommand::execute()
{
    m_layer->deleteMeasureRectFromSet(m_rect);
}

void
Layer::DeleteMeasurementRectCommand::unexecute()
{
    m_layer->addMeasureRectToSet(m_rect);
}

void
Layer::measureStart(View *v, QMouseEvent *e)
{
    setMeasureRectFromPixrect(v, m_draggingRect,
                              QRect(e->x(), e->y(), 0, 0));
    m_haveDraggingRect = true;
}

void
Layer::measureDrag(View *v, QMouseEvent *e)
{
    if (!m_haveDraggingRect) return;

    setMeasureRectFromPixrect(v, m_draggingRect,
                              QRect(m_draggingRect.pixrect.x(),
                                    m_draggingRect.pixrect.y(),
                                    e->x() - m_draggingRect.pixrect.x(),
                                    e->y() - m_draggingRect.pixrect.y()));
}

void
Layer::measureEnd(View *v, QMouseEvent *e)
{
    if (!m_haveDraggingRect) return;
    measureDrag(v, e);

    if (!m_draggingRect.pixrect.isNull()) {
        CommandHistory::getInstance()->addCommand
            (new AddMeasurementRectCommand(this, m_draggingRect));
    }

    m_haveDraggingRect = false;
}

void
Layer::measureDoubleClick(View *v, QMouseEvent *e)
{
    // nothing, in the base class
}

void
Layer::deleteCurrentMeasureRect()
{
    if (!m_haveCurrentMeasureRect) return;
    
    MeasureRectSet::const_iterator focusRectItr =
        findFocusedMeasureRect(m_currentMeasureRectPoint);

    if (focusRectItr == m_measureRects.end()) return;

    CommandHistory::getInstance()->addCommand
        (new DeleteMeasurementRectCommand(this, *focusRectItr));
}

void
Layer::paintMeasurementRects(View *v, QPainter &paint,
                             bool showFocus, QPoint focusPoint) const
{
    updateMeasurePixrects(v);

    MeasureRectSet::const_iterator focusRectItr = m_measureRects.end();

    if (m_haveDraggingRect) {

        paintMeasurementRect(v, paint, m_draggingRect, true);

    } else if (showFocus) {

        focusRectItr = findFocusedMeasureRect(focusPoint);
    }

    m_haveCurrentMeasureRect = false;

    for (MeasureRectSet::const_iterator i = m_measureRects.begin(); 
         i != m_measureRects.end(); ++i) {

        bool focused = (i == focusRectItr);
        paintMeasurementRect(v, paint, *i, focused);

        if (focused) {
            m_haveCurrentMeasureRect = true;
            m_currentMeasureRectPoint = focusPoint;
        }
    }
}

bool
Layer::nearestMeasurementRectChanged(View *v, QPoint prev, QPoint now) const
{
    updateMeasurePixrects(v);
    
    MeasureRectSet::const_iterator i0 = findFocusedMeasureRect(prev);
    MeasureRectSet::const_iterator i1 = findFocusedMeasureRect(now);

    return (i0 != i1);
}

void
Layer::updateMeasurePixrects(View *v) const
{
    long sf = v->getStartFrame();
    long ef = v->getEndFrame();

    for (MeasureRectSet::const_iterator i = m_measureRects.begin(); 
         i != m_measureRects.end(); ++i) {

        // This logic depends on the fact that if one measure rect in
        // a layer has frame values, they all will.  That is in fact
        // the case, because haveFrames is based on whether the layer
        // hasTimeXAxis() or not.  Measure rect ordering in the rect
        // set wouldn't work correctly either, if haveFrames could
        // vary.

        if (i->haveFrames) {
            if (i->startFrame >= ef) break;
            if (i->endFrame <= sf) continue;
        }

        int x0 = i->pixrect.x();
        int x1 = x0 + i->pixrect.width();

        if (i->haveFrames) {
            if (i->startFrame >= v->getStartFrame()) {
                x0 = v->getXForFrame(i->startFrame);
            }
            if (i->endFrame <= long(v->getEndFrame())) {
                x1 = v->getXForFrame(i->endFrame);
            }
        }
        
        i->pixrect = QRect(x0, i->pixrect.y(), x1 - x0, i->pixrect.height());

        updateMeasureRectYCoords(v, *i);
    }
}

void
Layer::updateMeasureRectYCoords(View *v, const MeasureRect &r) const
{
    int y0 = lrint(r.startY * v->height());
    int y1 = lrint(r.endY * v->height());
    r.pixrect = QRect(r.pixrect.x(), y0, r.pixrect.width(), y1 - y0);
}

void
Layer::setMeasureRectYCoord(View *v, MeasureRect &r, bool start, int y) const
{
    if (start) {
        r.startY = double(y) / double(v->height());
        r.endY = r.startY;
    } else {
        r.endY = double(y) / double(v->height());
    }
}

void
Layer::setMeasureRectFromPixrect(View *v, MeasureRect &r, QRect pixrect) const
{
    r.pixrect = pixrect;
    r.haveFrames = hasTimeXAxis();
    if (r.haveFrames) {
        r.startFrame = v->getFrameForX(pixrect.x());
        r.endFrame = v->getFrameForX(pixrect.x() + pixrect.width());
    }
    setMeasureRectYCoord(v, r, true, pixrect.y());
    setMeasureRectYCoord(v, r, false, pixrect.y() + pixrect.height());
}

Layer::MeasureRectSet::const_iterator
Layer::findFocusedMeasureRect(QPoint focusPoint) const
{
    float frDist = 0;
    MeasureRectSet::const_iterator focusRectItr = m_measureRects.end();

    for (MeasureRectSet::const_iterator i = m_measureRects.begin(); 
         i != m_measureRects.end(); ++i) {

        if (!i->pixrect.adjusted(-2, -2, 2, 2).contains(focusPoint)) continue;

        int cx = i->pixrect.x() + i->pixrect.width()/2;
        int cy = i->pixrect.y() + i->pixrect.height()/2;
        int xd = focusPoint.x() - cx;
        int yd = focusPoint.y() - cy;
        
        float d = sqrt(xd * xd + yd * yd);
        
        if (focusRectItr == m_measureRects.end() || d < frDist) {
            focusRectItr = i;
            frDist = d;
        }
    }

    return focusRectItr;
}

void
Layer::paintMeasurementRect(View *v, QPainter &paint,
                            const MeasureRect &r, bool focus) const
{
    if (r.haveFrames) {
        
        int x0 = -1;
        int x1 = v->width() + 1;
        
        if (r.startFrame >= v->getStartFrame()) {
            x0 = v->getXForFrame(r.startFrame);
        }
        if (r.endFrame <= long(v->getEndFrame())) {
            x1 = v->getXForFrame(r.endFrame);
        }
        
        QRect pr = QRect(x0, r.pixrect.y(), x1 - x0, r.pixrect.height());
        r.pixrect = pr;
    }

    v->drawMeasurementRect(paint, this, r.pixrect.normalized(), focus);
}

void
Layer::toXml(QTextStream &stream,
             QString indent, QString extraAttributes) const
{
    stream << indent;

    stream << QString("<layer id=\"%2\" type=\"%1\" name=\"%3\" model=\"%4\" %5")
	.arg(encodeEntities(LayerFactory::getInstance()->getLayerTypeName
                            (LayerFactory::getInstance()->getLayerType(this))))
	.arg(getObjectExportId(this))
	.arg(encodeEntities(objectName()))
	.arg(getObjectExportId(getModel()))
	.arg(extraAttributes);

    if (m_measureRects.empty()) {
        stream << QString("/>\n");
        return;
    }

    stream << QString(">\n");

    for (MeasureRectSet::const_iterator i = m_measureRects.begin();
         i != m_measureRects.end(); ++i) {
        i->toXml(stream, indent + "  ");
    }

    stream << QString("</layer>\n");
}

void
Layer::toBriefXml(QTextStream &stream,
                  QString indent, QString extraAttributes) const
{
    stream << indent;

    stream << QString("<layer id=\"%2\" type=\"%1\" name=\"%3\" model=\"%4\" %5/>\n")
	.arg(encodeEntities(LayerFactory::getInstance()->getLayerTypeName
                            (LayerFactory::getInstance()->getLayerType(this))))
	.arg(getObjectExportId(this))
	.arg(encodeEntities(objectName()))
	.arg(getObjectExportId(getModel()))
        .arg(extraAttributes);
}

