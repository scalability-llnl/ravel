//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
//
// This file is part of Ravel.
// Written by Kate Isaacs, kisaacs@acm.org, All rights reserved.
// LLNL-CODE-663885
//
// For details, see https://github.com/scalability-llnl/ravel
// Please also see the LICENSE file for our notice and the LGPL.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License (as published by
// the Free Software Foundation) version 2.1 dated February 1999.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the terms and
// conditions of the GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//////////////////////////////////////////////////////////////////////////////
#include "traditionalvis.h"
#include <iostream>
#include <cmath>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QWheelEvent>

#include "trace.h"
#include "function.h"
#include "otfcollective.h"
#include "message.h"
#include "colormap.h"
#include "commevent.h"
#include "event.h"
#include "entity.h"
#include "primaryentitygroup.h"
#include "p2pevent.h"
#include "collectiveevent.h"

TraditionalVis::TraditionalVis(QWidget * parent, VisOptions * _options)
    : TimelineVis(parent = parent, _options),
    minTime(0),
    maxTime(0),
    startTime(0),
    timeSpan(0),
    lassoRect(QRect()),
    blockheight(0)
{

}

TraditionalVis::~TraditionalVis()
{
}

void TraditionalVis::setTrace(Trace * t)
{
    VisWidget::setTrace(t);

    // Initial conditions
    startEntity = 0;
    entitySpan = trace->num_pes;
    maxEntities = trace->num_pes;
    startPartition = 0;

    // Determine/save time information
    minTime = t->min_time;
    maxTime = t->max_time;
    double initTime = minTime;
    for (QVector<QVector<Event *> *>::Iterator events = trace->events->begin();
         events != trace->events->end(); ++events)
    {
        if (!(*events)->empty()
            && trace->functions->value((*events)->first()->function)->name == "MPI_Init"
            && (*events)->first()->exit > initTime)
            initTime = (*events)->first()->exit;
    }

    startTime = initTime;
    timeSpan = std::min((double) initTimeSpan, maxTime - initTime);
}

void TraditionalVis::mouseDoubleClickEvent(QMouseEvent * event)
{
    if (!visProcessed)
        return;

    int x = event->x();
    int y = event->y();
    for (QMap<Event *, QRect>::Iterator evt = drawnEvents.begin();
         evt != drawnEvents.end(); ++evt)
        if (evt.value().contains(x,y))
        {
            if (evt.key() == selected_event)
            {
                selected_event = NULL;
            }
            else
            {
                selected_event = evt.key();
            }
            break;
        }

    changeSource = true;
    emit eventClicked(selected_event);
    repaint();
}

void TraditionalVis::rightDrag(QMouseEvent * event)
{
    if (!visProcessed)
        return;

    mousex = event->x();
    mousey = event->y();
    if (pressx < event->x())
    {
        startTime = startTime + timeSpan * pressx / rect().width();
        timeSpan = timeSpan * (event->x() - pressx) / rect().width();
    }
    else
    {
        startTime = startTime + timeSpan * event->x() / rect().width();
        timeSpan = timeSpan * (pressx - event->x()) / rect().width();
    }
    if (startTime < minTime)
        startTime = minTime;
    if (startTime > maxTime)
        startTime = maxTime;

    if (pressy < event->y())
    {
        startEntity = startEntity + entitySpan * pressy
                / (rect().height() - timescaleHeight);
        entitySpan = entitySpan * (event->y() - pressy)
                / (rect().height() - timescaleHeight);
    }
    else
    {
        startEntity = startEntity + entitySpan * event->y()
                / (rect().height() - timescaleHeight);
        entitySpan = entitySpan * (pressy - event->y())
                / (rect().height() - timescaleHeight);
    }
    if (startEntity + entitySpan > trace->num_pes)
        startEntity = trace->num_pes - entitySpan;
    if (startEntity < 0)
        startEntity = 0;


    repaint();
    changeSource = true;
    emit timeChanged(startTime, startTime + timeSpan, false);
}


void TraditionalVis::mouseMoveEvent(QMouseEvent * event)
{
    if (!visProcessed)
        return;

    lassoRect = QRect();
    if (mousePressed && !rightPressed) {
        lastStartTime = startTime;
        int diffx = mousex - event->x();
        int diffy = mousey - event->y();
        startTime += timeSpan / 1.0 / rect().width() * diffx;
        startEntity += diffy / 1.0 / entityheight;

        if (startTime < minTime)
            startTime = minTime;
        if (startTime > maxTime)
            startTime = maxTime;

        if (startEntity + entitySpan > trace->num_pes)
            startEntity = trace->num_pes - entitySpan;
        if (startEntity < 0)
            startEntity = 0;


        mousex = event->x();
        mousey = event->y();
        repaint();
    }
    else if (mousePressed && rightPressed)
    {
        lassoRect = QRect(std::min(pressx, event->x()),
                          std::min(pressy, event->y()),
                          abs(pressx - event->x()),
                          abs(pressy - event->y()));
        repaint();
    }
    else // potential hover
    {
        mousex = event->x();
        mousey = event->y();
        if (hover_event == NULL
                || !drawnEvents[hover_event].contains(mousex, mousey))
        {
            // Hover for all events! Note since we only save comm events in the
            // drawnEvents, this will recalculate for non-comm events each move
            if (mousey > blockheight * entitySpan)
                hover_event = NULL;
            else
            {
                int hover_proc = floor((mousey - 1) / blockheight) + startEntity;
                unsigned long long hover_time = (mousex - 1 - labelWidth)
                                                / 1.0 / rect().width()
                                                * timeSpan + startTime;
                hover_event = trace->findEvent(hover_proc, hover_time);
            }
            repaint();
        }
    }

    // startTime & timeSpan are calculated during painting when we
    // examine each element
    if (mousePressed) {
        changeSource = true;
        emit timeChanged(startTime, startTime + timeSpan, false);
    }
}

void TraditionalVis::wheelEvent(QWheelEvent * event)
{
    if (!visProcessed)
        return;

    float scale = 1;
    int clicks = event->delta() / 8 / 15;
    scale = 1 + clicks * 0.05;
    if (Qt::MetaModifier && event->modifiers()) {
        // Vertical
        float avgProc = startEntity + entitySpan / 2.0;
        entitySpan *= scale;
        startEntity = avgProc - entitySpan / 2.0;
    } else {
        // Horizontal
        lastStartTime = startTime;
        double middleTime = startTime + timeSpan / 2.0;
        timeSpan *= scale;
        startTime = middleTime - timeSpan / 2.0;
        if (startTime < 0)
        {
            timeSpan += startTime;
            startTime = 0;
        }
    }
    repaint();
    changeSource = true;
    // startTime & timeSpan are calculated during painting when we
    // examine each element
    emit timeChanged(startTime, startTime + timeSpan, false);
}


void TraditionalVis::setTime(float start, float stop, bool jump)
{
    if (!visProcessed)
        return;

    if (changeSource) {
        changeSource = false;
        return;
    }
    lastStartTime = startTime;
    startTime = start;
    timeSpan = stop - start;
    jumped = jump;

    if (!closed) {
        repaint();
    }
}

void TraditionalVis::prepaint()
{
    if (!visProcessed)
        return;
    closed = false;
}


void TraditionalVis::drawNativeGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!visProcessed)
        return;

    int effectiveHeight = rect().height() - timescaleHeight;
    if (effectiveHeight / entitySpan >= 3 && drawnEvents.size() < 6000)
        return;

    drawnEvents.clear();

    // Setup viewport
    int width = rect().width();
    int height = effectiveHeight;
    float effectiveSpan = timeSpan;

    glViewport(0,
               timescaleHeight,
               width,
               height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, effectiveSpan, 0, entitySpan, 0, 1);

    float barheight = 1.0;
    entityheight = height/ entitySpan;

    // Generate buffers to hold each bar. We don't know how many there will
    // be since we draw one per event.
    QVector<GLfloat> * bars = new QVector<GLfloat>();
    QVector<GLfloat> * colors = new QVector<GLfloat>();

    // Process events for values
    float position; // placement of entity

    int start = std::max(int(floor(startEntity)), 0);
    int end = std::min(int(ceil(startEntity + entitySpan)),
                       trace->num_pes - 1);
    for (int i = start; i <= end; ++i)
    {
        position = i;
        QVector<Event *> * roots = trace->roots->at(i);
        for (QVector<Event *>::Iterator root = roots->begin();
             root != roots->end(); ++root)
        {
            paintNotStepEventsGL(*root, position, barheight, bars, colors);
        }
    }

    // Draw
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColorPointer(3,GL_FLOAT,0,colors->constData());
    glVertexPointer(2,GL_FLOAT,0,bars->constData());
    glDrawArrays(GL_QUADS,0,bars->size()/2);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    delete bars;
    delete colors;
}

// To make sure events have correct overlapping, this draws from the root down
// TODO: Have a level cut off so we don't descend all the way down the tree
void TraditionalVis::paintNotStepEventsGL(Event * evt,
                                          float position, float barheight,
                                          QVector<GLfloat> * bars,
                                          QVector<GLfloat> * colors)
{
    if (evt->enter > startTime + timeSpan || evt->exit < startTime)
        return; // Out of time

    drawnEvents[evt] = QRect(0,0,0,0);
    int x, y, w, h;
    w = evt->exit - evt->enter;
    y = (entitySpan + startEntity - position) * barheight - 1;
    x = 0;
    if (evt->enter >= startTime)
        x = evt->enter - startTime;
    else
        w -= (startTime - evt->enter);

    QColor color = options->colormap->color(evt->getMetric(options->metric)); //    (*(*evt)->metrics)[metric]->event);
    //if (evt->hasMetric(options->metric))
    //    color= options->colormap->color(evt->getMetric(options->metric));
    //else
    {
        if (evt == selected_event)
        {
            color = Qt::yellow;
        }
        else
        {
            int graycolor = std::max(100, 100 + evt->depth * 20);
            color = QColor(graycolor, graycolor, graycolor);
        }
    }

    bars->append(x);
    bars->append(y);
    bars->append(x);
    bars->append(y + barheight);
    bars->append(x + w);
    bars->append(y + barheight);
    bars->append(x + w);
    bars->append(y);
    for (int j = 0; j < 4; ++j)
    {
        colors->append(color.red() / 255.0);
        colors->append(color.green() / 255.0);
        colors->append(color.blue() / 255.0);
    }

    for (QVector<Event *>::Iterator child = evt->callees->begin();
         child != evt->callees->end(); ++child)
    {
        paintNotStepEventsGL(*child, position, barheight, bars, colors);
    }


}


void TraditionalVis::qtPaint(QPainter *painter)
{
    if(!visProcessed)
        return;

    if ((rect().height() - timescaleHeight) / entitySpan >= 3 && drawnEvents.size() <= 6000)
    {
        drawnEvents.clear();
        paintEvents(painter);
    }

    drawEntityLabels(painter, rect().height() - timescaleHeight,
                      entityheight);
    QString seconds = drawTimescale(painter, startTime, timeSpan);
    emit(timeScaleString(seconds));
    drawHover(painter);

    if (!lassoRect.isNull())
    {
        painter->setPen(Qt::yellow);
        painter->drawRect(lassoRect);
        painter->fillRect(lassoRect, QBrush(QColor(255, 255, 144, 150)));
    }
}

void TraditionalVis::paintEvents(QPainter *painter)
{
    int canvasHeight = rect().height() - timescaleHeight;

    int entity_spacing = 0;
    if (canvasHeight / entitySpan > 12)
        entity_spacing = 3;

    blockheight = floor(canvasHeight / entitySpan);
    float barheight = blockheight - entity_spacing;
    entityheight = blockheight;
    QRect extents = QRect(labelWidth, 0, rect().width(), canvasHeight);

    painter->setFont(QFont("Helvetica", 10));

    int position;
    QSet<CommBundle *> drawComms = QSet<CommBundle *>();
    QSet<CommBundle *> selectedComms = QSet<CommBundle *>();
    painter->setPen(QPen(QColor(0, 0, 0)));

    int start = std::max(int(floor(startEntity)), 0);
    int end = std::min(int(ceil(startEntity + entitySpan)),
                       trace->num_pes - 1);
    for (int i = start; i <= end; ++i)
    {
        position = i;
        QVector<Event *> * roots = trace->roots->at(i);
        for (QVector<Event *>::Iterator root = roots->begin();
             root != roots->end(); ++root)
        {
            if (filterApplied && filterEvents.contains(*root))
                paintNotStepEvents(painter, *root, position, entity_spacing,
                                   barheight, blockheight, &extents, &drawComms,
                                   &selectedComms);
            else if (!filterApplied)
                paintNotStepEvents(painter, *root, position, entity_spacing,
                                   barheight, blockheight, &extents, &drawComms,
                                   &selectedComms);
        }
    }

    // Messages
    // We need to do all of the message drawing after the event drawing
    // for overlap purposes
    if (options->showMessages != VisOptions::MSG_NONE)
    {
        for (QSet<CommBundle *>::Iterator comm = drawComms.begin();
             comm != drawComms.end(); ++comm)
        {
            if (!selectedComms.contains(*comm))
                (*comm)->draw(painter, this);
        }

        for (QSet<CommBundle *>::Iterator comm = selectedComms.begin();
             comm != selectedComms.end(); ++comm)
        {
            (*comm)->draw(painter, this);
        }
    }

    painter->fillRect(QRectF(0,canvasHeight,rect().width(),rect().height()),
                      QBrush(QColor(Qt::white)));
}

void TraditionalVis::drawMessage(QPainter * painter, Message * msg)
{
    int penwidth = 1;
    if (entitySpan <= 32)
        penwidth = 2;

    Qt::GlobalColor pencolor = Qt::black;
    if (selected_event == msg->sender || selected_event == msg->receiver)
        pencolor = Qt::yellow;


    int y = getY(msg->sender) + blockheight / 2;
    int x = getX(msg->sender);
    QPointF p1 = QPointF(x, y);
    y = getY(msg->receiver) + blockheight / 2;
    x = getX(msg->receiver) + getW(msg->receiver);
    QPointF p2 = QPointF(x, y);
    painter->setPen(QPen(pencolor, penwidth, Qt::SolidLine));
    painter->drawLine(p1, p2);
}

void TraditionalVis::drawCollective(QPainter * painter, CollectiveRecord * cr)
{
    int root, x, y, prev_x, prev_y;
    CollectiveEvent * coll_event;
    QPointF p1, p2;

    int ell_w = 3;
    int ell_h = 3;
    int h = blockheight;
    int coll_type = (*(trace->collective_definitions))[cr->collective]->type;
    bool rooted = false;

    // Rooted
    if (coll_type == 2 || coll_type == 3)
    {
        rooted = true;
        root = cr->root;
    }

    painter->setPen(QPen(Qt::darkGray, 1, Qt::SolidLine));
    painter->setBrush(QBrush(Qt::darkGray));
    coll_event = cr->events->at(0);
    int w = getW(coll_event);
    prev_y = getY(coll_event);
    prev_x = getX(coll_event) + w;

    if (rooted && coll_event->entity == root)
    {
        painter->setBrush(QBrush());
    }
    painter->drawEllipse(prev_x - ell_w/2,
                         prev_y + h/2 - ell_h/2,
                         ell_w, ell_h);

    for (int i = 1; i < cr->events->size(); i++)
    {
        painter->setPen(QPen(Qt::darkGray, 1, Qt::SolidLine));
        painter->setBrush(QBrush(Qt::darkGray));
        coll_event = cr->events->at(i);

        if (rooted && coll_event->entity == root)
        {
            painter->setBrush(QBrush());
        }

        y = getY(coll_event);
        x = getX(coll_event) + getW(coll_event);
        painter->drawEllipse(x - ell_w/2, y + h/2 - ell_h/2,
                             ell_w, ell_h);

        // Arc style
        painter->setPen(QPen(Qt::black, 1, Qt::DashLine));
        painter->setBrush(QBrush());
        p1 = QPointF(prev_x, prev_y + h/2.0);
        p2 = QPointF(x, y + h/2.0);
        painter->drawLine(p1, p2);

        prev_x = x;
        prev_y = y;
    }
}

int TraditionalVis::getY(CommEvent * evt)
{
    int y = 0;
    int position = evt->pe;
    y = floor((position - startEntity) * blockheight) + 1;
    return y;
}

int TraditionalVis::getX(CommEvent *evt)
{
    return floor(static_cast<long long>(evt->enter - startTime)
                                        / 1.0 / timeSpan * rect().width())
                                        + 1 + labelWidth;
}


int TraditionalVis::getW(CommEvent *evt)
{
    return (evt->exit - evt->enter) / 1.0 / timeSpan * rect().width();
}

// To make sure events have correct overlapping, this draws from the root down
// TODO: Have a level cut off so we don't descend all the way down the tree
void TraditionalVis::paintNotStepEvents(QPainter *painter, Event * evt,
                                        float position, int entity_spacing,
                                        float barheight, float blockheight,
                                        QRect * extents, QSet<CommBundle *> * drawComms,
                                        QSet<CommBundle *> * selectedComms)
{
    if (evt->enter > startTime + timeSpan || evt->exit < startTime)
        return; // Out of time
    if (evt->isCommEvent())
    {
        evt->addComms(drawComms);
        if (evt == selected_event)
            evt->addComms(selectedComms);
    }

    int x, y, w, h;
    w = (evt->exit - evt->enter) / 1.0 / timeSpan * rect().width();
    if (w >= 2) // Don't draw tiny events
    {
        y = floor((position - startEntity) * blockheight) + 1;
        x = floor(static_cast<long long>(evt->enter - startTime) / 1.0
                  / timeSpan * rect().width()) + 1 + labelWidth;
        h = barheight;

        // Corrections for partially drawn
        bool complete = true;
        if (y < 0) {
            h = barheight - fabs(y);
            y = 0;
            complete = false;
        } else if (y + h > rect().height() - timescaleHeight) {
            h = rect().height() - timescaleHeight - y;
            complete = false;
        }
        if (x < labelWidth) {
            w -= (labelWidth - x);
            x = labelWidth;
            complete = false;
        } else if (x + w > rect().width()) {
            w = rect().width() - x;
            complete = false;
        }


        // Change pen color if selected
        if (evt == selected_event)
            painter->setPen(QPen(Qt::yellow));


        if (evt == selected_event)
            painter->fillRect(QRectF(x, y, w, h), QBrush(Qt::yellow));
        else
        {
            // Draw event
            int graycolor = 0;
            graycolor = std::min(240, std::max(100, 100 + evt->depth * 20));
            painter->fillRect(QRectF(x, y, w, h),
                              QBrush(QColor(graycolor, graycolor, graycolor)));
        }
        drawnEvents[evt] = QRect(x, y, w, h);

        // Draw border
        if (entity_spacing > 0)
        {
            if (complete)
                painter->drawRect(QRectF(x,y,w,h));
            else
                incompleteBox(painter, x, y, w, h, extents);
        }

        // Revert pen color
        if (evt == selected_event)
            painter->setPen(QPen(QColor(0, 0, 0)));

        unsigned long long drawnEnter = std::max((unsigned long long) startTime, evt->enter);
        unsigned long long available_w = (evt->getVisibleEnd(drawnEnter)
                           - drawnEnter)
                            / 1.0 / timeSpan * rect().width() + 2;

        QString fxnName = ((*(trace->functions))[evt->function])->name;
        QRect fxnRect = painter->fontMetrics().boundingRect(fxnName);
        if (fxnRect.width() < available_w && fxnRect.height() < h)
            painter->drawText(x + 2, y + fxnRect.height(), fxnName);
    }

    for (QVector<Event *>::Iterator child = evt->callees->begin();
         child != evt->callees->end(); ++child)
    {
        paintNotStepEvents(painter, *child, position, entity_spacing,
                           barheight, blockheight, extents, drawComms,
                           selectedComms);
    }


}
