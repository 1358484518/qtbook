# Qt5 (qmake) project for the 10M-point real-time QCustomPlot demo.
# Tested with Qt 5.15 (API-compatible with Qt 5.14).
#
# Build:
#   qmake            # Qt5 qmake (e.g. /usr/lib/qt5/bin/qmake, or `qmake -qt=5`)
#   make -j
# Run (with a screen):
#   ./qcp_realtime_demo
# Run head-less (no display):
#   ./qcp_realtime_demo -platform offscreen

QT       += widgets printsupport
CONFIG   += c++17 release
TEMPLATE  = app
TARGET    = qcp_realtime_demo

SOURCES  += main.cpp qcustomplot.cpp
HEADERS  += qcustomplot.h

QMAKE_CXXFLAGS += -O2

# ---- Optional GPU acceleration (uncomment on real hardware) --------------
# On Qt5 the OpenGL module is simply "opengl" (Qt6 renamed it to openglwidgets).
# QCustomPlot needs the QCUSTOMPLOT_USE_OPENGL define to enable setOpenGl(true).
# QT      += opengl
# DEFINES += QCUSTOMPLOT_USE_OPENGL
