/***************************************************************************
 *   Copyright (C) 2012~2012 by xubin                                      *
 *   nybux.tsui@gmail.com                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

typedef struct lua_State lua_State;
typedef struct _LuaModule LuaModule;
typedef struct _LuaExtension LuaExtension;
typedef struct _FcitxInstance FcitxInstance;

// IME Lua trigger callback
// in is user input
// out is retrun by lua
typedef void (*TriggerFn)(LuaModule *luamodule, const char *in, const char *out);

// alloc/free luamodule
LuaModule * LuaModuleAlloc(FcitxInstance *fcitx);
void LuaModuleFree(LuaModule *luamodule);
FcitxInstance *GetFcitx(LuaModule *luamodule);

// Load lua extension, name is filename of lua source file
LuaExtension * LoadExtension(LuaModule *luamodule, const char *name); 

// Unload extension by name
void UnloadExtension(LuaModule *luamodule, const char *name); 

// call lua trigger, input is user input
// callback is called when valid candidate generated
int InputTrigger(LuaModule *luamodule, const char *input, TriggerFn callback); 