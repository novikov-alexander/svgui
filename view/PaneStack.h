
/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef _PANESTACK_H_
#define _PANESTACK_H_

#include <QFrame>

class QWidget;
class QLabel;
class QStackedWidget;
class QSplitter;
class QHBoxLayout;
class View;
class Pane;
class Layer;
class ViewManager;
class PropertyContainer;
class PropertyStack;

class PaneStack : public QFrame
{
    Q_OBJECT

public:
    PaneStack(QWidget *parent, ViewManager *viewManager);

    Pane *addPane(bool suppressPropertyBox = false); // I own the returned value
    void deletePane(Pane *pane); // Deletes the pane, but _not_ its layers

    int getPaneCount() const; // Returns only count of visible panes
    Pane *getPane(int n); // Of visible panes; I own the returned value

    void hidePane(Pane *pane); // Also removes pane from getPane/getPaneCount
    void showPane(Pane *pane); // Returns pane to getPane/getPaneCount

    int getHiddenPaneCount() const;
    Pane *getHiddenPane(int n); // I own the returned value

    void setCurrentPane(Pane *pane);
    void setCurrentLayer(Pane *pane, Layer *layer);
    Pane *getCurrentPane();

    enum LayoutStyle {
        NoPropertyStacks = 0,
        SinglePropertyStackLayout = 1,
        PropertyStackPerPaneLayout = 2
    };

    LayoutStyle getLayoutStyle() const { return m_layoutStyle; }
    void setLayoutStyle(LayoutStyle style);

signals:
    void currentPaneChanged(Pane *pane);
    void currentLayerChanged(Pane *pane, Layer *layer);
    void rightButtonMenuRequested(Pane *pane, QPoint position);
    void propertyStacksResized();

public slots:
    void propertyContainerAdded(PropertyContainer *);
    void propertyContainerRemoved(PropertyContainer *);
    void propertyContainerSelected(View *client, PropertyContainer *);
    void paneInteractedWith();
    void rightButtonMenuRequested(QPoint);

protected:
    Pane *m_currentPane;

    struct PaneRec
    {
	Pane        *pane;
	QWidget     *propertyStack;
	QLabel      *currentIndicator;
        QFrame      *frame;
        QHBoxLayout *layout;
    };

    std::vector<PaneRec> m_panes;
    std::vector<PaneRec> m_hiddenPanes;

    QSplitter *m_splitter;
    QStackedWidget *m_propertyStackStack;

    ViewManager *m_viewManager; // I don't own this
    void sizePropertyStacks();

    LayoutStyle m_layoutStyle;
};

#endif

