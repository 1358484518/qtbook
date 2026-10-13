# Qt5 (qmake) project for the LIVE dynamic display:
#   a real scrolling window (ring buffer + evicting min/max summary +
#   acquisition QThread + 60 fps render timer). Runs until you close it.
# Tested with Qt 5.15 (API-compatible with Qt 5.14).
#
# Build:
#   qmake qcp_realtime_live.pro
#   make -j
# Run (live window):
#   ./qcp_realtime_live
# Head-less frame capture (for recording a video):
#   ./qcp_realtime_live --record 40 -platform offscreen

QT       += widgets printsupport
CONFIG   += c++17 release
TEMPLATE  = app
TARGET    = qcp_realtime_live

SOURCES  += realtime_live.cpp qcustomplot.cpp
HEADERS  += qcustomplot.h

QMAKE_CXXFLAGS += -O2

# ---- Optional GPU acceleration (uncomment on real hardware) --------------
# QT      += opengl
# DEFINES += QCUSTOMPLOT_USE_OPENGL
