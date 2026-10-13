# Qt5 (qmake) project for the ENHANCED real-time demo:
#   fixed-capacity ring buffer + evicting min/max summary +
#   acquisition thread (QThread) decoupled from the 60 fps render timer.
# Tested with Qt 5.15 (API-compatible with Qt 5.14).
#
# Build:
#   qmake qcp_realtime_threaded.pro
#   make -j
# Run (with a screen):
#   ./qcp_realtime_threaded
# Run head-less:
#   ./qcp_realtime_threaded -platform offscreen

QT       += widgets printsupport
CONFIG   += c++17 release
TEMPLATE  = app
TARGET    = qcp_realtime_threaded

SOURCES  += ring_threaded.cpp qcustomplot.cpp
HEADERS  += qcustomplot.h

QMAKE_CXXFLAGS += -O2

# ---- Optional GPU acceleration (uncomment on real hardware) --------------
# QT      += opengl
# DEFINES += QCUSTOMPLOT_USE_OPENGL
