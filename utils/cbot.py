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
        self.is_winner = False
        self.turn = 0

        self.dataset = None

    def prepare_new_game(self, p1_wins, grid):
        self.is_swapped = False
        self.is_winner = p1_wins
        self.p1_wins = p1_wins
        self.turn = 0

        self.libc.py_set_ctx(byref(self.ctx))
        self.libc.py_reset()
        self.libc.py_get_ctx(byref(self.ctx))
        nwalls = 0.

        for i, arr in enumerate(grid):
            for j, v in enumerate(arr):
                x, y = i+1, j+1

                if int(v) == 0:
                    self.ctx.board[x + SIZE*y].wall = 1
                    nwalls += 1.0
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
            "nturns": 0, # Ok
            "p1_won": p1_wins, # Ok
            "wall_ratio": nwalls / (12.0**2), # Ok
            "rank": None, # Ok

            "w_ninvalid": 0, # OK
            "l_ninvalid": 0, # OK

            "w_actions": [], # Ok
            "l_actions": [], # Ok

            "w_nmines": 0, # Ok
            "l_nmines": 0, # Ok

            "w_ntowers": 0, # Ok
            "l_ntowers": 0, # Ok

            "w_actives": [], # Ok
            "l_actives": [], # Ok

            "w_units": [], # Ok
            "l_units": [], # Ok

            "w_income": [], # Ok
            "l_income": [], # Ok

            "w_upkeep": [], # Ok
            "l_upkeep": [], # Ok

            "w_lvl1": 0, # Ok
            "w_lvl2": 0, # Ok
            "w_lvl3": 0, # Ok

            "l_lvl1": 0, # Ok
            "l_lvl2": 0, # Ok
            "l_lvl3": 0, # Ok
        }

    def _add_endturn(self):
        self.libc.py_get_ctx(byref(self.ctx))

        upkeep = 0
        for i in range(self.ctx.p1.nunits):
            u = self.ctx.p1.units[i]
            if u.lvl == 1:
                upkeep += 1
            elif u.lvl == 2:
                upkeep += 4
            else:
                upkeep += 20

        if self.is_winner:
            self.dataset["w_units"].append(self.ctx.p1.nunits)
            self.dataset["w_upkeep"].append(upkeep)
            self.dataset["w_income"].append(self.ctx.p1.income)
            self.dataset["w_actives"].append(self.ctx.p1.nactive)
        else:
            self.dataset["l_units"].append(self.ctx.p1.nunits)
            self.dataset["l_upkeep"].append(upkeep)
            self.dataset["l_income"].append(self.ctx.p1.income)
            self.dataset["l_actives"].append(self.ctx.p1.nactive)


    def run_game(self, frames):
        turn = 0
        previous_player = 0
        self.libc.py_new_turn()

        if self.is_winner:
            self.dataset["w_actions"].append(0)
        else:
            self.dataset["l_actions"].append(0)

        for fid, f in enumerate(frames):
            lvl = f["lvl"]
            agentid = f["agentid"]
            summary = f["summary"]

            if len(summary) != 0:
                # agentid is not reliable
                agentid = int(summary[1])

            if agentid != previous_player:
                self._add_endturn()
                self.swap_players()

                if self.is_winner:
                    self.dataset["w_actions"].append(0)
                else:
                    self.dataset["l_actions"].append(0)

                previous_player = agentid
                if agentid == 0:
                    self.turn += 1
                    self.dataset["nturns"] += 1


            if len(summary) == 0:
                cmd_successful = False
                continue

            if len(summary.split("\n")) >= 13:
                self.parsing_success = False
                return

            if self.is_winner:
                self.dataset["w_ninvalid"] += len(summary.split("\n")) - 2
            else:
                self.dataset["l_ninvalid"] += len(summary.split("\n")) - 2

            cmd_successful, cmd = try_parse_action(summary, lvl, self.is_swapped)
            if not cmd_successful:
                # Failed to parse
                continue

            if self.game_is_over():
                print "Game over too early"

            if cmd.type == CMD_BUILD and cmd.param == TYPE_MINE:
                if self.is_winner:
                    self.dataset["w_nmines"] += 1.
                else:
                    self.dataset["l_nmines"] += 1.

            if cmd.type == CMD_BUILD and cmd.param == TYPE_TOWER:
                if self.is_winner:
                    self.dataset["w_ntowers"] += 1.
                else:
                    self.dataset["l_ntowers"] += 1.

            if cmd.type == CMD_TRAIN:
                if self.is_winner:
                    self.dataset["w_lvl%d" % cmd.param] += 1.
                else:
                    self.dataset["l_lvl%d" % cmd.param] += 1.

            if self.is_winner:
                self.dataset["w_actions"][-1] += 1
            else:
                self.dataset["l_actions"][-1] += 1

            #print
            #print "BEFORE"
            #print turn, summary, lvl, agentid
            #self.libc.py_print_state()
            self.execute_action(cmd)
            #print "AFTER"
            #self.libc.py_print_state()
            #raw_input()
            turn += 1

        self._add_endturn()
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
        self.is_swapped = not self.is_swapped
        self.is_winner = not self.is_winner


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

