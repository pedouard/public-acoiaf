# -*- coding: utf-8 -*-
import warnings
warnings.filterwarnings("ignore", message="numpy.dtype size changed")
warnings.filterwarnings("ignore", message="numpy.ufunc size changed")

import os
import sys
import random
import numpy as np
import pandas as pd
import time
import json
import base64
import subprocess

from ctypes import *
from constants import *

__DIR__ = os.path.dirname(os.path.abspath(__file__))
BOT_PATH = os.path.join(__DIR__, "../src/")

class XY(Structure):
    _fields_ = [("x", c_int64),
                ("y", c_int64),
                ]

class UNIT(Structure):
    _fields_ = [("xy", XY),

                ("id", c_int64),
                ("lvl", c_int64),
                ("upkeep", c_int64),
                ("has_moved", c_int64),

                ("decaying", c_int64),
                ]

class CELL(Structure):
    _fields_ = [("xy", XY),
                ("wall", c_int64),
                ("hq", c_int64),
                ("minable", c_int64),

                ("owner", c_int64),
                ("tower", c_int64),
                ("mine", c_int64),
                ("building", c_int64),

                ("p1_cost", c_int64),
                ("p2_cost", c_int64),

                ("protected", c_int64),
                ("active", c_int64),
                ("has_unit", c_int64),
                ("unit", POINTER(UNIT)),

                ("expl", c_int64),
                ]

class PLAYER(Structure):
    _fields_ = [("hq", XY),
                ("is_red", c_int64),
                ("identity", c_int64),

                ("nunits", c_int64),
                ("units", SIZE_SQ*UNIT),
                ("gold", c_int64),

                ("lost", c_int64),
                ("bankrupt", c_int64),
                ("nmines", c_int64),
                ("mine_cost", c_int64),
                ("income", c_int64),
                ("nactive", c_int64),
                ]

class CMD(Structure):
    _fields_ = [("type", c_int64),
                ("param", c_int64),
                ("xy", XY),
                ]

class CTX(Structure):
    _fields_ = [("turn", c_int64),
                ("id_counter", c_int64),

                ("board", (SIZE*SIZE)*CELL),

                ("doable", SIZE_SQ*6*CMD),
                ("ndoable", c_int64),

                ("p1", PLAYER),
                ("p2", PLAYER),
                ]

def parse_action_str(action_str):
    arr = action_str.split(" ")
    if len(arr) == 6:
        return arr[2], int(arr[-2][1:-1]), int(arr[-1][:-1])
    else:
        return arr[3], int(arr[-2][1:-1]), int(arr[-1][:-1])


def try_parse_action(summary, lvl, is_swapped):
    cmd = CMD()

    for action_str in summary.split("\n"):
        if "Invalid" in action_str or "stayed still" in action_str or "Unrecognised" in action_str or len(action_str) == 0:
            continue

        if "built" in action_str:
            cmd.type = CMD_BUILD
            param, x, y = parse_action_str(action_str)

            if param == "TOWER":
                param = TYPE_TOWER
            else:
                param = TYPE_MINE

        elif "train" in action_str:
            cmd.type = CMD_TRAIN
            _, x, y = parse_action_str(action_str)
            param = lvl

        elif "move" in action_str:
            cmd.type = CMD_MOVE
            param, x, y = parse_action_str(action_str)
            param = int(param) + 10000 - 1

        else:
            print "Unknown action %s" % action_str
            return False, None

        cmd.param = param
        if is_swapped:
            cmd.xy.x = SIZE - x - 2
            cmd.xy.y = SIZE - y - 2
        else:
            cmd.xy.x = x + 1
            cmd.xy.y = y + 1

        return True, cmd

    return False, None


class CBot():

    def __init__(self):
        self.libc = None
        self.build_and_link()
        self.ctx = CTX()

        self.p1_wins = None
        self.parsing_success = False
        self.is_swapped = False

        self.dataset = None

    def prepare_new_game(self, p1_wins, grid):
        self.is_swapped = False
        self.p1_wins = p1_wins

        self.libc.py_set_ctx(byref(self.ctx))
        self.libc.py_reset()
        self.libc.py_get_ctx(byref(self.ctx))

        for i, arr in enumerate(grid):
            for j, v in enumerate(arr):
                x, y = i+1, j+1

                if int(v) == 0:
                    self.ctx.board[x + SIZE*y].wall = 1
                elif int(v) == 1:
                    pass
                elif int(v) == 2:
                    self.ctx.board[x + SIZE*y].minable = 1
                elif int(v) == 3:
                    pass

        self.ctx.board[1 + SIZE*1].owner = 1
        self.ctx.board[(SIZE-2) + SIZE*(SIZE-2)].owner = -1
        self.ctx.board[(SIZE-2) + SIZE*(SIZE-3)].owner = -1

        self.ctx.p1.gold = 21
        self.ctx.p2.gold = 20

        self.libc.py_set_ctx(byref(self.ctx))

        self.dataset = {
        }

    def run_game(self, frames):
        turn = 0
        previous_player = 0
        self.libc.py_new_turn()

        for fid, f in enumerate(frames):
            lvl = f["lvl"]
            agentid = f["agentid"]
            summary = f["summary"]

            if len(summary) != 0:
                # agentid is not reliable
                agentid = int(summary[1])

            if agentid != previous_player:
                self.swap_players()
                previous_player = agentid

            if len(summary) == 0:
                cmd_successful = False
                continue

            if len(summary.split("\n")) >= 13:
                self.parsing_success = False
                return

            cmd_successful, cmd = try_parse_action(summary, lvl, self.is_swapped)
            if not cmd_successful:
                # Failed to parse
                continue

            if self.game_is_over():
                print "Game over too early"

            #print
            #print "BEFORE"
            #print turn, summary, lvl, agentid
            #self.libc.py_print_state()
            self.execute_action(cmd)
            #print "AFTER"
            #self.libc.py_print_state()
            #raw_input()
            turn += 1

        if cmd_successful:
            assert self.game_is_over(), "Game should be over!"
            self.parsing_success = True
        else:
            # Timeout or failed command
            assert not self.game_is_over(), "Game should not be over because timeout!"


    def execute_action(self, cmd):
        # Store actions made by the winner
        self.libc.py_list_doable()
        self.libc.py_get_ctx(byref(self.ctx))
        self.libc.py_apply_action(byref(cmd))

    def game_is_over(self):
        self.libc.py_get_ctx(byref(self.ctx))

        if self.ctx.p1.gold < 0 or self.ctx.p2.gold < 0:
            raise Exception("Gold is negative !")

        if self.ctx.p1.lost == 1 or self.ctx.p2.lost == 1:
            return True

        return False

    def swap_players(self):
        self.libc.py_swap()
        self.libc.py_new_turn()
        self.is_swapped = ~self.is_swapped


    """ MISC """

    def build_and_link(self):
        self._build()
        self.libc = self._link()

    def _build(self):
        cmd = "make clean -C %s" % BOT_PATH
        print cmd
        p = subprocess.Popen(cmd.split(" "), stdout=subprocess.PIPE)
        p.communicate()

        cmd = "make -C %s" % BOT_PATH
        print cmd
        p = subprocess.Popen(cmd.split(" "), stdout=subprocess.PIPE)
        p.communicate()

    def _link(self):
        return CDLL(BOT_PATH + "libcbot.so")

