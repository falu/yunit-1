/*
 * Copyright (C) 2013 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "DirectionalDragArea.h"

#include <QtCore/qmath.h>
#include <QtCore/QTimer>
#include <QDebug>

using namespace UbuntuGestures;

#define DIRECTIONALDRAGAREA_DEBUG 0

#if DIRECTIONALDRAGAREA_DEBUG
#define DDA_DEBUG(msg) qDebug("[DDA] " msg)
namespace {
QString touchPointStateToString(Qt::TouchPointState state) {
    switch (state) {
    case Qt::TouchPointPressed:
        return QString("pressed");
    case Qt::TouchPointMoved:
        return QString("moved");
    case Qt::TouchPointStationary:
        return QString("stationary");
    default: // Qt::TouchPointReleased:
        return QString("released");
    }
}
QString touchEventToString(QTouchEvent *ev)
{
    QString message;

    switch (ev->type()) {
    case QEvent::TouchBegin:
        message.append("TouchBegin ");
        break;
    case QEvent::TouchUpdate:
        message.append("TouchUpdate ");
        break;
    case QEvent::TouchEnd:
        message.append("TouchEnd ");
        break;
    default: //QEvent::TouchCancel
        message.append("TouchCancel ");
    }

    for (int i=0; i < ev->touchPoints().size(); ++i) {

        const QTouchEvent::TouchPoint& touchPoint = ev->touchPoints().at(i);
        message.append(
            QString("(id:%1, state:%2, scenePos:(%3,%4)) ")
                .arg(touchPoint.id())
                .arg(touchPointStateToString(touchPoint.state()))
                .arg(touchPoint.scenePos().x())
                .arg(touchPoint.scenePos().y())
            );
    }

    return message;
}

const char *statusToString(DirectionalDragArea::Status status)
{
    if (status == DirectionalDragArea::WaitingForTouch) {
        return "WaitingForTouch";
    } else if (status == DirectionalDragArea::Undecided) {
        return "Undecided";
    } else {
        return "Recognized";
    }
}

} // namespace {
#else // DIRECTIONALDRAGAREA_DEBUG
#define DDA_DEBUG(msg) do{}while(0)
#endif // DIRECTIONALDRAGAREA_DEBUG

// Essentially a QTimer wrapper
class RecognitionTimer : public UbuntuGestures::AbstractTimer
{
    Q_OBJECT
public:
    RecognitionTimer(QObject *parent) : UbuntuGestures::AbstractTimer(parent) {
        m_timer.setSingleShot(false);
        connect(&m_timer, &QTimer::timeout,
                this, &UbuntuGestures::AbstractTimer::timeout);
    }
    virtual int interval() const { return m_timer.interval(); }
    virtual void setInterval(int msecs) { m_timer.setInterval(msecs); }
    virtual void start() { m_timer.start(); UbuntuGestures::AbstractTimer::start(); }
    virtual void stop() { m_timer.stop(); UbuntuGestures::AbstractTimer::stop(); }
private:
    QTimer m_timer;
};

DirectionalDragArea::DirectionalDragArea(QQuickItem *parent)
    : QQuickItem(parent)
    , m_status(WaitingForTouch)
    , m_touchId(-1)
    , m_direction(Direction::Rightwards)
    , m_wideningAngle(0)
    , m_wideningFactor(0)
    , m_distanceThreshold(0)
    , m_minSpeed(0)
    , m_maxSilenceTime(200)
    , m_silenceTime(0)
    , m_compositionTime(60)
    , m_numSamplesOnLastSpeedCheck(0)
    , m_recognitionTimer(0)
    , m_velocityCalculator(0)
    , m_timeSource(new RealTimeSource)
    , m_activeTouches(m_timeSource)
{
    setRecognitionTimer(new RecognitionTimer(this));
    m_recognitionTimer->setInterval(60);

    m_velocityCalculator = new AxisVelocityCalculator(this);
}

Direction::Type DirectionalDragArea::direction() const
{
    return m_direction;
}

void DirectionalDragArea::setDirection(Direction::Type direction)
{
    if (direction != m_direction) {
        m_direction = direction;
        Q_EMIT directionChanged(m_direction);
    }
}

void DirectionalDragArea::setMaxDeviation(qreal value)
{
    if (m_dampedScenePos.maxDelta() != value) {
        m_dampedScenePos.setMaxDelta(value);
        Q_EMIT maxDeviationChanged(value);
    }
}

qreal DirectionalDragArea::wideningAngle() const
{
    return m_wideningAngle;
}

void DirectionalDragArea::setWideningAngle(qreal angle)
{
    if (angle == m_wideningAngle)
        return;

    m_wideningAngle = angle;
    m_wideningFactor = qTan(angle * M_PI / 180.0);
    Q_EMIT wideningAngleChanged(angle);
}

void DirectionalDragArea::setDistanceThreshold(qreal value)
{
    if (m_distanceThreshold != value) {
        m_distanceThreshold = value;
        Q_EMIT distanceThresholdChanged(value);
    }
}

void DirectionalDragArea::setMinSpeed(qreal value)
{
    if (m_minSpeed != value) {
        m_minSpeed = value;
        Q_EMIT minSpeedChanged(value);
    }
}

void DirectionalDragArea::setMaxSilenceTime(int value)
{
    if (m_maxSilenceTime != value) {
        m_maxSilenceTime = value;
        Q_EMIT maxSilenceTimeChanged(value);
    }
}

void DirectionalDragArea::setCompositionTime(int value)
{
    if (m_compositionTime != value) {
        m_compositionTime = value;
        Q_EMIT compositionTimeChanged(value);
    }
}

void DirectionalDragArea::setRecognitionTimer(UbuntuGestures::AbstractTimer *timer)
{
    int interval = 0;
    bool timerWasRunning = false;

    // can be null when called from the constructor
    if (m_recognitionTimer) {
        interval = m_recognitionTimer->interval();
        timerWasRunning = m_recognitionTimer->isRunning();
        if (m_recognitionTimer->parent() == this) {
            delete m_recognitionTimer;
        }
    }

    m_recognitionTimer = timer;
    timer->setInterval(interval);
    connect(timer, &UbuntuGestures::AbstractTimer::timeout,
            this, &DirectionalDragArea::checkSpeed);
    if (timerWasRunning) {
        m_recognitionTimer->start();
    }
}

void DirectionalDragArea::setTimeSource(UbuntuGestures::SharedTimeSource timeSource)
{
    m_timeSource = timeSource;
    m_velocityCalculator->setTimeSource(timeSource);
    m_activeTouches.m_timeSource = timeSource;
}

qreal DirectionalDragArea::distance() const
{
    if (Direction::isHorizontal(m_direction)) {
        return m_previousPos.x() - m_startPos.x();
    } else {
        return m_previousPos.y() - m_startPos.y();
    }
}

qreal DirectionalDragArea::sceneDistance() const
{
    if (Direction::isHorizontal(m_direction)) {
        return m_previousScenePos.x() - m_startScenePos.x();
    } else {
        return m_previousScenePos.y() - m_startScenePos.y();
    }
}

qreal DirectionalDragArea::touchX() const
{
    return m_previousPos.x();
}

qreal DirectionalDragArea::touchY() const
{
    return m_previousPos.y();
}

qreal DirectionalDragArea::touchSceneX() const
{
    return m_previousScenePos.x();
}

qreal DirectionalDragArea::touchSceneY() const
{
    return m_previousScenePos.y();
}

void DirectionalDragArea::touchEvent(QTouchEvent *event)
{
    #if DIRECTIONALDRAGAREA_DEBUG
    qDebug() << "[DDA]" << m_timeSource->msecsSinceReference()
        << qPrintable(touchEventToString(event));
    #endif

    if (!isEnabled() || !isVisible()) {
        QQuickItem::touchEvent(event);
        return;
    }

    switch (m_status) {
        case WaitingForTouch:
            touchEvent_absent(event);
            break;
        case Undecided:
            touchEvent_undecided(event);
            break;
        default: // Recognized:
            touchEvent_recognized(event);
            break;
    }

    m_activeTouches.update(event);
}

void DirectionalDragArea::touchEvent_absent(QTouchEvent *event)
{
    if (!event->touchPointStates().testFlag(Qt::TouchPointPressed)) {
        // Nothing to see here. No touch starting in this event.
        return;
    }

    if (isWithinTouchCompositionWindow()) {
        // too close to the last touch start. So we consider them as starting roughly at the same time.
        // Can't be a single-touch gesture.
        #if DIRECTIONALDRAGAREA_DEBUG
        qDebug("[DDA] A new touch point came in but we're still within time composition window. Ignoring it.");
        #endif
        return;
    }

    const QList<QTouchEvent::TouchPoint> &touchPoints = event->touchPoints();

    const QTouchEvent::TouchPoint *newTouchPoint = nullptr;
    for (int i = 0; i < touchPoints.count(); ++i) {
        const QTouchEvent::TouchPoint &touchPoint = touchPoints.at(i);
        if (touchPoint.state() == Qt::TouchPointPressed) {
            if (newTouchPoint) {
                // more than one touch starting in this QTouchEvent. Can't be a single-touch gesture
                return;
            } else {
                // that's our candidate
                m_touchId = touchPoint.id();
                newTouchPoint = &touchPoint;
            }
        }
    }

    Q_ASSERT(newTouchPoint);

    // If we have made this far, we are good to go to the next status.

    m_startPos = newTouchPoint->pos();
    m_startScenePos = newTouchPoint->scenePos();
    m_touchId = newTouchPoint->id();
    m_dampedScenePos.reset(m_startScenePos);
    updateVelocityCalculator(m_startScenePos);
    m_velocityCalculator->reset();
    m_numSamplesOnLastSpeedCheck = 0;
    m_silenceTime = 0;
    setPreviousPos(m_startPos);
    setPreviousScenePos(m_startScenePos);

    setStatus(Undecided);
}

void DirectionalDragArea::touchEvent_undecided(QTouchEvent *event)
{
    const QTouchEvent::TouchPoint *touchPoint = fetchTargetTouchPoint(event);

    if (!touchPoint) {
        qCritical() << "DirectionalDragArea[status=Undecided]: touch " << m_touchId
            << "missing from QTouchEvent without first reaching state Qt::TouchPointReleased. "
               "Considering it as released.";
        setStatus(WaitingForTouch);
        return;
    }

    const QPointF &touchScenePos = touchPoint->scenePos();

    if (touchPoint->state() == Qt::TouchPointReleased) {
        // touch has ended before recognition concluded
        DDA_DEBUG("Touch has ended before recognition concluded");
        setStatus(WaitingForTouch);
        return;
    }

    if (event->touchPointStates().testFlag(Qt::TouchPointPressed) && isWithinTouchCompositionWindow()) {
        // multi-finger drags are not accepted
        DDA_DEBUG("Multi-finger drags are not accepted");
        setStatus(WaitingForTouch);
        return;
    }

    m_previousDampedScenePos.setX(m_dampedScenePos.x());
    m_previousDampedScenePos.setY(m_dampedScenePos.y());
    m_dampedScenePos.update(touchScenePos);
    updateVelocityCalculator(touchScenePos);

    if (!pointInsideAllowedArea()) {
        DDA_DEBUG("Rejecting gesture because touch point is outside allowed area.");
        setStatus(WaitingForTouch);
        return;
    }

    if (!movingInRightDirection()) {
        DDA_DEBUG("Rejecting gesture becauuse touch point is moving in the wrong direction.");
        setStatus(WaitingForTouch);
        return;
    }

    setPreviousPos(touchPoint->pos());
    setPreviousScenePos(touchScenePos);

    if (isWithinTouchCompositionWindow()) {
        // There's still time for some new touch to appear and ruin our party as it would be combined
        // with our m_touchId one and therefore deny the possibility of a single-finger gesture.
        DDA_DEBUG("Sill within composition window. Let's wait more.");
        return;
    }

    if (movedFarEnough(touchScenePos)) {
        setStatus(Recognized);
    } else {
        DDA_DEBUG("Didn't move far enough yet. Let's wait more.");
    }
}

void DirectionalDragArea::touchEvent_recognized(QTouchEvent *event)
{
    const QTouchEvent::TouchPoint *touchPoint = fetchTargetTouchPoint(event);

    if (!touchPoint) {
        qCritical() << "DirectionalDragArea[status=Recognized]: touch " << m_touchId
            << "missing from QTouchEvent without first reaching state Qt::TouchPointReleased. "
               "Considering it as released.";
        setStatus(WaitingForTouch);
    } else {
        setPreviousPos(touchPoint->pos());
        setPreviousScenePos(touchPoint->scenePos());

        if (touchPoint->state() == Qt::TouchPointReleased) {
            setStatus(WaitingForTouch);
        }
    }
}

const QTouchEvent::TouchPoint *DirectionalDragArea::fetchTargetTouchPoint(QTouchEvent *event)
{
    const QList<QTouchEvent::TouchPoint> &touchPoints = event->touchPoints();
    const QTouchEvent::TouchPoint *touchPoint = 0;
    for (int i = 0; i < touchPoints.size(); ++i) {
        if (touchPoints.at(i).id() == m_touchId) {
            touchPoint = &touchPoints.at(i);
            break;
        }
    }
    return touchPoint;
}

bool DirectionalDragArea::pointInsideAllowedArea() const
{
    qreal dX = m_dampedScenePos.x() - m_startScenePos.x();
    qreal dY = m_dampedScenePos.y() - m_startScenePos.y();

    switch (m_direction) {
        case Direction::Upwards:
            return dY <= 0 && qFabs(dX) <= qFabs(dY) * m_wideningFactor;
        case Direction::Downwards:
            return dY >= 0 && qFabs(dX) <= dY * m_wideningFactor;
        case Direction::Leftwards:
            return dX <= 0  && qFabs(dY) <= qFabs(dX) * m_wideningFactor;
        default: // Direction::Rightwards:
            return dX >= 0 && qFabs(dY) <= dX * m_wideningFactor;
    }
}

bool DirectionalDragArea::movingInRightDirection() const
{
    switch (m_direction) {
        case Direction::Upwards:
            return m_dampedScenePos.y() <= m_previousDampedScenePos.y();
        case Direction::Downwards:
            return m_dampedScenePos.y() >= m_previousDampedScenePos.y();
        case Direction::Leftwards:
            return m_dampedScenePos.x() <= m_previousDampedScenePos.x();
        default: // Direction::Rightwards:
            return m_dampedScenePos.x() >= m_previousDampedScenePos.x();
    }
}

bool DirectionalDragArea::movedFarEnough(const QPointF &point) const
{
    if (m_distanceThreshold > 0) {
        if (Direction::isHorizontal(m_direction))
            return qFabs(point.x() - m_startScenePos.x()) > m_distanceThreshold;
        else
            return qFabs(point.y() - m_startScenePos.y()) > m_distanceThreshold;
    } else {
        return true;
    }
}

void DirectionalDragArea::checkSpeed()
{
    if (m_velocityCalculator->numSamples() >= AxisVelocityCalculator::MIN_SAMPLES_NEEDED) {
        qreal speed = qFabs(m_velocityCalculator->calculate());
        qreal minSpeedMsecs = m_minSpeed / 1000.0;

        if (speed < minSpeedMsecs) {
            DDA_DEBUG("Rejecting gesture because it's below minimum speed.");
            setStatus(WaitingForTouch);
        }
    }

    if (m_velocityCalculator->numSamples() == m_numSamplesOnLastSpeedCheck) {
        m_silenceTime += m_recognitionTimer->interval();

        if (m_silenceTime > m_maxSilenceTime) {
            DDA_DEBUG("Rejecting gesture because it's silence time has been exceeded.");
            setStatus(WaitingForTouch);
        }
    } else {
        m_silenceTime = 0;
    }
    m_numSamplesOnLastSpeedCheck = m_velocityCalculator->numSamples();
}

void DirectionalDragArea::setStatus(DirectionalDragArea::Status newStatus)
{
    if (newStatus == m_status)
        return;

    DirectionalDragArea::Status oldStatus = m_status;

    if (oldStatus == Undecided) {
        m_recognitionTimer->stop();
    }

    m_status = newStatus;
    Q_EMIT statusChanged(m_status);

    #if DIRECTIONALDRAGAREA_DEBUG
    qDebug() << "[DDA]" << statusToString(oldStatus) << "->" << statusToString(newStatus);
    #endif

    switch (newStatus) {
        case WaitingForTouch:
            Q_EMIT draggingChanged(false);
            break;
        case Undecided:
            m_recognitionTimer->start();
            Q_EMIT draggingChanged(true);
            break;
        case Recognized:
            if (oldStatus == WaitingForTouch)
                Q_EMIT draggingChanged(true);
            break;
        default:
            // no-op
            break;
    }
}

void DirectionalDragArea::setPreviousPos(const QPointF &point)
{
    bool xChanged = m_previousPos.x() != point.x();
    bool yChanged = m_previousPos.y() != point.y();

    m_previousPos = point;

    if (xChanged) {
        Q_EMIT touchXChanged(point.x());
        if (Direction::isHorizontal(m_direction))
            Q_EMIT distanceChanged(distance());
    }

    if (yChanged) {
        Q_EMIT touchYChanged(point.y());
        if (Direction::isVertical(m_direction))
            Q_EMIT distanceChanged(distance());
    }
}

void DirectionalDragArea::setPreviousScenePos(const QPointF &point)
{
    bool xChanged = m_previousScenePos.x() != point.x();
    bool yChanged = m_previousScenePos.y() != point.y();

    m_previousScenePos = point;

    if (xChanged) {
        Q_EMIT touchSceneXChanged(point.x());
        if (Direction::isHorizontal(m_direction))
            Q_EMIT sceneDistanceChanged(sceneDistance());
    }

    if (yChanged) {
        Q_EMIT touchSceneYChanged(point.y());
        if (Direction::isVertical(m_direction))
            Q_EMIT sceneDistanceChanged(sceneDistance());
    }
}

void DirectionalDragArea::updateVelocityCalculator(const QPointF &point)
{
    if (Direction::isHorizontal(m_direction)) {
        m_velocityCalculator->setTrackedPosition(point.x());
    } else {
        m_velocityCalculator->setTrackedPosition(point.y());
    }
}

bool DirectionalDragArea::isWithinTouchCompositionWindow()
{
    return !m_activeTouches.isEmpty() &&
        m_timeSource->msecsSinceReference() <=
            m_activeTouches.mostRecentStartTime() + (qint64)compositionTime();
}

//**************************  ActiveTouchesInfo **************************

DirectionalDragArea::ActiveTouchesInfo::ActiveTouchesInfo(UbuntuGestures::SharedTimeSource timeSource)
    : m_timeSource(timeSource)
{
    // Estimate of the maximum number of active touches we might reach.
    // Not a problem if it ends up being an underestimate as this is just
    // an optimization.
    reserve(10);
}

void DirectionalDragArea::ActiveTouchesInfo::update(QTouchEvent *event)
{
    if (!(event->touchPointStates() & (Qt::TouchPointPressed | Qt::TouchPointReleased))) {
        // nothing to update
        return;
    }

    const QList<QTouchEvent::TouchPoint> &touchPoints = event->touchPoints();
    for (int i = 0; i < touchPoints.count(); ++i) {
        const QTouchEvent::TouchPoint &touchPoint = touchPoints.at(i);
        if (touchPoint.state() == Qt::TouchPointPressed) {
            addTouchPoint(touchPoint);
        } else if (touchPoint.state() == Qt::TouchPointReleased) {
            removeTouchPoint(touchPoint);
        }
    }
}

void DirectionalDragArea::ActiveTouchesInfo::addTouchPoint(const QTouchEvent::TouchPoint &touchPoint)
{
    ActiveTouchInfo activeTouchInfo;
    activeTouchInfo.id = touchPoint.id();
    activeTouchInfo.startTime = m_timeSource->msecsSinceReference();
    append(activeTouchInfo);
}

void DirectionalDragArea::ActiveTouchesInfo::removeTouchPoint(const QTouchEvent::TouchPoint &touchPoint)
{
    for (int i = 0; i < count(); ++i) {
        if (touchPoint.id() == at(i).id) {
            remove(i);
            return;
        }
    }
    Q_ASSERT(false); // shouldn't reach this point
}

qint64 DirectionalDragArea::ActiveTouchesInfo::mostRecentStartTime()
{
    Q_ASSERT(count() > 0);

    qint64 highestStartTime;
    int i = 0;
    do {
        highestStartTime = at(i).startTime;
        ++i;
    } while (i < count());

    return highestStartTime;
}

// Because we are defining a new QObject-based class (RecognitionTimer) here.
#include "DirectionalDragArea.moc"
