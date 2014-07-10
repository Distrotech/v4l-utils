/* qv4l2: a control panel controlling v4l2 devices.
 *
 * Copyright (C) 2006 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "capture-win.h"

#include <QCloseEvent>
#include <QLabel>
#include <QImage>
#include <QVBoxLayout>
#include <QApplication>
#include <QDesktopWidget>

#define MIN_WIN_SIZE_WIDTH 160
#define MIN_WIN_SIZE_HEIGHT 120

bool CaptureWin::m_enableScaling = true;
double CaptureWin::m_pixelAspectRatio = 1.0;
CropMethod CaptureWin::m_cropMethod = QV4L2_CROP_NONE;

CaptureWin::CaptureWin()
{
	setWindowTitle("V4L2 Capture");
	m_hotkeyClose = new QShortcut(Qt::CTRL+Qt::Key_W, this);
	connect(m_hotkeyClose, SIGNAL(activated()), this, SLOT(close()));
	m_hotkeyScaleReset = new QShortcut(Qt::CTRL+Qt::Key_F, this);
	connect(m_hotkeyScaleReset, SIGNAL(activated()), this, SLOT(resetSize()));
	m_frame.format      =  0;
	m_frame.size.setWidth(0);
	m_frame.size.setHeight(0);
	m_frame.planeData[0] = NULL;
	m_frame.planeData[1] = NULL;
	m_crop.delta.setWidth(0);
	m_crop.delta.setHeight(0);
	m_crop.size.setWidth(0);
	m_crop.size.setHeight(0);
	m_crop.updated = 0;
	m_scaledSize.setWidth(0);
	m_scaledSize.setHeight(0);
	m_origFrameSize.setWidth(0);
	m_origFrameSize.setHeight(0);
	m_windowSize.setWidth(0);
	m_windowSize.setHeight(0);
}

CaptureWin::~CaptureWin()
{
	if (layout() == NULL)
		return;

	layout()->removeWidget(this);
	delete layout();
	delete m_hotkeyClose;
	delete m_hotkeyScaleReset;
}

void CaptureWin::setFrame(int width, int height, __u32 format,
			  unsigned char *data, unsigned char *data2, const QString &info)
{
	m_frame.planeData[0] = data;
	m_frame.planeData[1] = data2;
	m_frame.info         = info;

	m_frame.updated = false;
	if (width != m_frame.size.width() || height != m_frame.size.height()
	    || format != m_frame.format) {
		m_frame.size.setHeight(height);
		m_frame.size.setWidth(width);
		m_frame.format       = format;
		m_frame.updated      = true;
		updateSize();
	}
	m_information.setText(m_frame.info);

	setRenderFrame();
}

void CaptureWin::buildWindow(QWidget *videoSurface)
{
	int l, t, r, b;
	QVBoxLayout *vbox = new QVBoxLayout(this);
	m_information.setText("No Frame");
	vbox->addWidget(videoSurface, 2000);
	vbox->addWidget(&m_information, 1, Qt::AlignBottom);
	vbox->getContentsMargins(&l, &t, &r, &b);
	vbox->setSpacing(b);
}

void CaptureWin::resetSize()
{
	if (isMaximized())
		showNormal();

        // Force resize even if no size change
	int w = m_origFrameSize.width();
	int h = m_origFrameSize.height();
	m_origFrameSize.setWidth(0);
	m_origFrameSize.setHeight(0);
	resize(w, h);
}

int CaptureWin::cropHeight(int width, int height)
{
	double realWidth = width * m_pixelAspectRatio;
	int validHeight;

	switch (m_cropMethod) {
	case QV4L2_CROP_W149:
		validHeight = realWidth / (14.0 / 9.0);
		break;
	case QV4L2_CROP_W169:
		validHeight = realWidth / (16.0 / 9.0);
		break;
	case QV4L2_CROP_C185:
		validHeight = realWidth / 1.85;
		break;
	case QV4L2_CROP_C239:
		validHeight = realWidth / 2.39;
		break;
	case QV4L2_CROP_TB:
		validHeight = height - 2;
		break;
	default:
		return 0;
	}

	if (validHeight < MIN_WIN_SIZE_HEIGHT || validHeight >= height)
		return 0;

	return (height - validHeight) / 2;
}

int CaptureWin::cropWidth(int width, int height)
{
	if (m_cropMethod != QV4L2_CROP_P43)
		return 0;

	int validWidth = (height * 4.0 / 3.0) / m_pixelAspectRatio;

	if (validWidth < MIN_WIN_SIZE_WIDTH || validWidth >= width)
		return 0;

	return (width - validWidth) / 2;
}

void CaptureWin::updateSize()
{
	m_crop.updated = 0;
	if (m_frame.updated) {
		m_scaledSize = scaleFrameSize(m_windowSize, m_frame.size);
		m_crop.delta.setHeight(cropHeight(m_frame.size.width(), m_frame.size.height()));
		m_crop.delta.setWidth(cropWidth(m_frame.size.width(), m_frame.size.height()));
		m_crop.size.setHeight(m_frame.size.height() - (m_crop.delta.height() * 2));
		m_crop.size.setWidth(m_frame.size.width() - (m_crop.delta.width() * 2));
		m_crop.updated = 1;
	}
}

void CaptureWin::setCropMethod(CropMethod crop)
{
	m_cropMethod = crop;
	QResizeEvent event (QSize(width(), height()), QSize(width(), height()));
	QCoreApplication::sendEvent(this, &event);
}

QSize CaptureWin::pixelAspectFrameSize(QSize size)
{
	if (!m_enableScaling)
		return size;

	if (m_pixelAspectRatio > 1)
		size.rwidth() *= m_pixelAspectRatio;

	if (m_pixelAspectRatio < 1)
		size.rheight() /= m_pixelAspectRatio;

	return size;
}

QSize CaptureWin::getMargins()
{
	int l, t, r, b;
	layout()->getContentsMargins(&l, &t, &r, &b);
	return QSize(l + r, t + b + m_information.minimumSizeHint().height() + layout()->spacing());
}

void CaptureWin::enableScaling(bool enable)
{
	if (!enable) {
		QSize margins = getMargins();
		QWidget::setMinimumSize(m_origFrameSize.width() + margins.width(),
					m_origFrameSize.height() + margins.height());
	} else {
		QWidget::setMinimumSize(MIN_WIN_SIZE_WIDTH, MIN_WIN_SIZE_HEIGHT);
	}
	m_enableScaling = enable;
	QResizeEvent event (QSize(width(), height()), QSize(width(), height()));
	QCoreApplication::sendEvent(this, &event);
}

void CaptureWin::resize(int width, int height)
{
	int h, w;

	// Dont resize window if the frame size is the same in
	// the event the window has been paused when beeing resized.
	if (width == m_origFrameSize.width() && height == m_origFrameSize.height())
		return;

	m_origFrameSize.setWidth(width);
	m_origFrameSize.setHeight(height);

	QSize margins = getMargins();
	QSize aspectedFrameSize = pixelAspectFrameSize(QSize(width, height));

	h = margins.height() - cropHeight(width, height) * 2 + aspectedFrameSize.height();
	w = margins.width() - cropWidth(width, height) * 2 + aspectedFrameSize.width();
	height = h;
	width = w;

	QDesktopWidget *screen = QApplication::desktop();
	QRect resolution = screen->screenGeometry();

	if (width > resolution.width())
		width = resolution.width();
	if (width < MIN_WIN_SIZE_WIDTH)
		width = MIN_WIN_SIZE_WIDTH;

	if (height > resolution.height())
		height = resolution.height();
	if (height < MIN_WIN_SIZE_HEIGHT)
		height = MIN_WIN_SIZE_HEIGHT;

	QWidget::setMinimumSize(MIN_WIN_SIZE_WIDTH, MIN_WIN_SIZE_HEIGHT);
	QWidget::resize(width, height);
}

QSize CaptureWin::scaleFrameSize(QSize window, QSize frame)
{
	QSize croppedSize = frame - QSize((cropWidth(frame.width(), frame.height()) * 2),
					  (cropHeight(frame.width(), frame.height()) * 2));
	QSize actualSize = pixelAspectFrameSize(croppedSize);

	if (!m_enableScaling) {
		window.setWidth(actualSize.width());
		window.setHeight(actualSize.height());
	}

	qreal newW, newH;
	if (window.width() >= window.height()) {
		newW = (qreal)window.width() / actualSize.width();
		newH = (qreal)window.height() / actualSize.height();
	} else {
		newH = (qreal)window.width() / actualSize.width();
		newW = (qreal)window.height() / actualSize.height();
	}
	qreal resized = (qreal)std::min(newW, newH);

	return (actualSize * resized);
}

void CaptureWin::setPixelAspectRatio(double ratio)
{
	m_pixelAspectRatio = ratio;
	QResizeEvent event(QSize(width(), height()), QSize(width(), height()));
	QCoreApplication::sendEvent(this, &event);
}

void CaptureWin::closeEvent(QCloseEvent *event)
{
	QWidget::closeEvent(event);
	emit close();
}
