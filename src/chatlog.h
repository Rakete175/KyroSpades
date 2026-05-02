/*
	Copyright (c) 2017-2020 ByteBit

	This file is part of KyroSpades.

	KyroSpades is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	KyroSpades is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with KyroSpades.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CHATLOG_H
#define CHATLOG_H

#include "hud.h"

/*
	Chat Log HUD tab.

	A separate HUD reachable from the ESC menu when connected to a server.
	Displays the global chat history (channel 0) chronologically, preserves
	inline color codes, and lets the user select text with the mouse and copy
	it via Ctrl+C / right-click. The tab is hidden when not connected to a
	server.
*/
extern struct hud hud_chatlog;

#endif
