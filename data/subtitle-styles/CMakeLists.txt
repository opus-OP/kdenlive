# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Kdenlive contributors

FILE(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/*.ass")
INSTALL (FILES ${files} DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/subtitle-styles)
