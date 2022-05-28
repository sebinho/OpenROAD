// BSD 3-Clause License
//
// Copyright (c) 2020, MICL, DD-Lab, University of Michigan
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <stdio.h>

#include <cstring>
#include <iostream>
#include <unordered_set>

#include "ant/AntennaChecker.hh"
#include "odb/db.h"
#include "odb/dbTypes.h"
#include "odb/dbWireGraph.h"
#include "odb/wOrder.h"
#include "sta/StaMain.hh"
#include "utl/Logger.h"

namespace ant {

using odb::dbBox;
using odb::dbBTerm;
using odb::dbInst;
using odb::dbITerm;
using odb::dbITermObj;
using odb::dbMaster;
using odb::dbMasterType;
using odb::dbMTerm;
using odb::dbNet;
using odb::dbSet;
using odb::dbTechAntennaPinModel;
using odb::dbTechLayer;
using odb::dbTechLayerAntennaRule;
using odb::dbTechLayerType;
using odb::dbTechVia;
using odb::dbVia;
using odb::dbViaParams;
using odb::dbWire;
using odb::dbWireGraph;
using odb::dbWireType;
using odb::dbIoType;

using utl::ANT;

using std::unordered_set;

struct PARinfo
{
  // std::pair<odb::dbWireGraph::Node*, std::vector<odb::dbWireGraph::Node*>>
  // WirerootNode;
  odb::dbWireGraph::Node* WirerootNode;
  std::set<odb::dbITerm*> iterms;
  double wire_area;
  double side_wire_area;
  double iterm_areas[2];
  double PAR_value;
  double PSR_value;
  double diff_PAR_value;
  double diff_PSR_value;
};

struct ARinfo
{
  odb::dbWireGraph::Node* WirerootNode;
  odb::dbWireGraph::Node* GateNode;
  bool violated_net;
  double PAR_value;
  double PSR_value;
  double diff_PAR_value;
  double diff_PSR_value;
  double CAR_value;
  double CSR_value;
  double diff_CAR_value;
  double diff_CSR_value;
  double diff_area;
};

struct ANTENNAmodel
{
  odb::dbTechLayer* layer;

  double metal_factor;
  double diff_metal_factor;

  double cut_factor;
  double diff_cut_factor;

  double side_metal_factor;
  double diff_side_metal_factor;

  double minus_diff_factor;
  double plus_diff_factor;
  double diff_metal_reduce_factor;
};

extern "C" {
extern int Ant_Init(Tcl_Interp* interp);
}

AntennaChecker::AntennaChecker()
{
}

AntennaChecker::~AntennaChecker()
{
}

void AntennaChecker::init(odb::dbDatabase* db, Logger* logger)
{
  db_ = db;
  logger_ = logger;
}

template <class valueType>
double AntennaChecker::defDist(valueType value)
{
  double _dist_factor
      = 1.0 / (double) db_->getChip()->getBlock()->getDbUnitsPerMicron();
  return ((double) value) * _dist_factor;
}

void AntennaChecker::loadAntennaRules()
{
  odb::dbTech* tech = db_->getTech();
  odb::dbSet<odb::dbTechLayer> tech_layers = tech->getLayers();

  odb::dbSet<odb::dbTechLayer>::iterator itr;
  for (itr = tech_layers.begin(); itr != tech_layers.end(); ++itr) {
    odb::dbTechLayer* tech_layer = (odb::dbTechLayer*) *itr;

    double metal_factor = 1.0;
    double diff_metal_factor = 1.0;

    double cut_factor = 1.0;
    double diff_cut_factor = 1.0;

    double side_metal_factor = 1.0;
    double diff_side_metal_factor = 1.0;

    double minus_diff_factor = 0.0;
    double plus_diff_factor = 0.0;
    double diff_metal_reduce_factor = 1.0;

    if (tech_layer->hasDefaultAntennaRule()) {
      dbTechLayerAntennaRule* antenna_rule
          = tech_layer->getDefaultAntennaRule();

      odb::dbStringProperty* layer_prop = odb::dbStringProperty::find(
          tech_layer, "LEF57_ANTENNAGATEPLUSDIFF");
      if (layer_prop != nullptr) {
        std::string gate_plus_diff_info = layer_prop->getValue();
        int start = 0;
        std::string gate_plus_diff_value = "";
        for (int i = 0; i < gate_plus_diff_info.size(); i++) {
          if (gate_plus_diff_info.at(i) == ' ') {
            gate_plus_diff_value
                = gate_plus_diff_info.substr(start, i - start - 1);
            start = i;
          }
        }
        plus_diff_factor = std::stod(gate_plus_diff_value);
      }
      if (antenna_rule->isAreaFactorDiffUseOnly()) {
        diff_metal_factor = antenna_rule->getAreaFactor();

        diff_cut_factor = antenna_rule->getAreaFactor();
      } else {
        metal_factor = antenna_rule->getAreaFactor();
        diff_metal_factor = antenna_rule->getAreaFactor();

        cut_factor = antenna_rule->getAreaFactor();
        diff_cut_factor = antenna_rule->getAreaFactor();
      }
      if (antenna_rule->isSideAreaFactorDiffUseOnly()) {
        diff_side_metal_factor = antenna_rule->getSideAreaFactor();
      } else {
        side_metal_factor = antenna_rule->getSideAreaFactor();
        diff_side_metal_factor = antenna_rule->getSideAreaFactor();
      }

      minus_diff_factor = antenna_rule->getAreaMinusDiffFactor();
    }

    ANTENNAmodel layer_antenna = {tech_layer,
                                  metal_factor,
                                  diff_metal_factor,
                                  cut_factor,
                                  diff_cut_factor,
                                  side_metal_factor,
                                  diff_side_metal_factor,
                                  minus_diff_factor,
                                  plus_diff_factor,
                                  diff_metal_reduce_factor};
    layer_info[tech_layer] = layer_antenna;
  }
}

dbWireGraph::Node* AntennaChecker::findSegmentRoot(dbWireGraph::Node* node,
                                                   int wire_level)
{
  if (!node->in_edge())
    return node;

  if (node->in_edge()->type() == dbWireGraph::Edge::Type::VIA
      || node->in_edge()->type() == dbWireGraph::Edge::Type::TECH_VIA) {
    if (node->in_edge()->source()->layer()->getRoutingLevel() > wire_level)
      return node;

    dbWireGraph::Node* new_root
        = findSegmentRoot(node->in_edge()->source(), wire_level);

    if (new_root->layer()->getRoutingLevel() == wire_level)
      return new_root;
    else
      return node;
  }

  if (node->in_edge()->type() == dbWireGraph::Edge::Type::SEGMENT
      || node->in_edge()->type() == dbWireGraph::Edge::Type::SHORT)
    return findSegmentRoot(node->in_edge()->source(), wire_level);

  return node;
}

dbWireGraph::Node* AntennaChecker::findSegmentStart(dbWireGraph::Node* node)
{
  if ((node->object() && node->object()->getObjectType() == dbITermObj)
      || !node->in_edge())
    return node;
  else if (node->in_edge()->type() == dbWireGraph::Edge::Type::VIA
           || node->in_edge()->type() == dbWireGraph::Edge::Type::TECH_VIA)
    return node;
  else if (node->in_edge()->type() == dbWireGraph::Edge::Type::SEGMENT
           || node->in_edge()->type() == dbWireGraph::Edge::Type::SHORT)
    return findSegmentStart(node->in_edge()->source());
  else
    return NULL;
}

bool AntennaChecker::ifSegmentRoot(dbWireGraph::Node* node, int wire_level)
{
  if ((node->object() && node->object()->getObjectType() == dbITermObj)
      || !node->in_edge())
    return true;
  else if (node->in_edge()->type() == dbWireGraph::Edge::Type::VIA
           || node->in_edge()->type() == dbWireGraph::Edge::Type::TECH_VIA) {
    if (node->in_edge()->source()->layer()->getRoutingLevel() <= wire_level) {
      dbWireGraph::Node* new_root
          = findSegmentRoot(node->in_edge()->source(), wire_level);
      if (new_root->layer()->getRoutingLevel() == wire_level)
        return false;
      else
        return true;
    } else
      return true;
  } else
    return false;
}

void AntennaChecker::findWireBelowIterms(dbWireGraph::Node* node,
                                         double iterm_areas[2],
                                         int wire_level,
                                         std::set<dbITerm*>& iv,
                                         std::set<dbWireGraph::Node*>& nv)
{
  if (checkIterm(node, iterm_areas))
    iv.insert(
        dbITerm::getITerm(db_->getChip()->getBlock(), node->object()->getId()));

  nv.insert(node);

  if (node->in_edge()
      && node->in_edge()->source()->layer()->getRoutingLevel() <= wire_level) {
    if ((node->in_edge()->type() == dbWireGraph::Edge::Type::VIA
         || node->in_edge()->type() == dbWireGraph::Edge::Type::TECH_VIA)
        && nv.find(node->in_edge()->source()) == nv.end()) {
      findWireBelowIterms(findSegmentStart(node->in_edge()->source()),
                          iterm_areas,
                          wire_level,
                          iv,
                          nv);
    } else if ((node->in_edge()->type() == dbWireGraph::Edge::Type::SEGMENT
                || node->in_edge()->type() == dbWireGraph::Edge::Type::SHORT)
               && nv.find(node->in_edge()->source()) == nv.end()) {
      findWireBelowIterms(
          node->in_edge()->source(), iterm_areas, wire_level, iv, nv);
    }
  }

  dbWireGraph::Node::edge_iterator edge_itr;
  for (edge_itr = node->begin(); edge_itr != node->end(); ++edge_itr) {
    if ((*edge_itr)->type() == dbWireGraph::Edge::Type::VIA
        || (*edge_itr)->type() == dbWireGraph::Edge::Type::TECH_VIA) {
      if ((*edge_itr)->target()->layer()->getRoutingLevel() <= wire_level
          && nv.find((*edge_itr)->target()) == nv.end()) {
        findWireBelowIterms(findSegmentStart((*edge_itr)->target()),
                            iterm_areas,
                            wire_level,
                            iv,
                            nv);
      }
    }

    else if (((*edge_itr)->type() == dbWireGraph::Edge::Type::SEGMENT
              || (*edge_itr)->type() == dbWireGraph::Edge::Type::SHORT)
             && nv.find((*edge_itr)->target()) == nv.end()) {
      findWireBelowIterms(
          (*edge_itr)->target(), iterm_areas, wire_level, iv, nv);
    }
  }
}

std::pair<double, double> AntennaChecker::calculateWireArea(
    dbWireGraph::Node* node,
    int wire_level,
    std::set<dbWireGraph::Node*>& nv,
    std::set<dbWireGraph::Node*>& level_nodes)
{
  double wire_area = 0;
  double side_wire_area = 0;

  double wire_width = defDist(node->layer()->getWidth());

  uint wire_thickness_uint = 0;
  node->layer()->getThickness(wire_thickness_uint);

  int start_x, start_y;
  int end_x, end_y;
  node->xy(start_x, start_y);

  std::vector<std::pair<dbWireGraph::Edge*, std::string>> edge_vec;
  if (node->in_edge() != nullptr
      && nv.find(node->in_edge()->source()) == nv.end())
    edge_vec.push_back({node->in_edge(), "IN"});

  dbWireGraph::Node::edge_iterator edge_it;
  int out_edges_count = 0;

  for (edge_it = node->begin(); edge_it != node->end(); edge_it++) {
    if (nv.find((*edge_it)->source()) == nv.end()) {
      out_edges_count++;
      edge_vec.push_back({*edge_it, "OUT"});
    }
  }

  nv.insert(node);

  for (auto edge_info : edge_vec) {
    dbWireGraph::Edge* edge = edge_info.first;
    std::string edge_io_type = edge_info.second;
    if (edge->type() == dbWireGraph::Edge::Type::VIA
        || edge->type() == dbWireGraph::Edge::Type::TECH_VIA) {
      if (edge_io_type.compare("IN") == 0) {
        wire_area += 0.5 * wire_width * wire_width;
        side_wire_area += defDist(wire_thickness_uint) * wire_width;

        if (edge->source()->layer()->getRoutingLevel() <= wire_level) {
          std::pair<double, double> areas
              = calculateWireArea(edge->source(), wire_level, nv, level_nodes);
          wire_area += areas.first;
          side_wire_area += areas.second;
        }
      }

      if (edge_io_type.compare("OUT") == 0) {
        if (out_edges_count == 1) {
          wire_area += 0.5 * wire_width * wire_width;
          side_wire_area += defDist(wire_thickness_uint) * wire_width;
        }

        if (edge->target()->layer()->getRoutingLevel() <= wire_level) {
          std::pair<double, double> areas
              = calculateWireArea(edge->target(), wire_level, nv, level_nodes);
          wire_area += areas.first;
          side_wire_area += areas.second;
        }
      }
    }

    if (edge->type() == dbWireGraph::Edge::Type::SEGMENT
        || edge->type() == dbWireGraph::Edge::Type::SHORT) {
      if (edge_io_type.compare("IN") == 0) {
        if (node->layer()->getRoutingLevel() == wire_level) {
          level_nodes.insert(node);
          edge->source()->xy(end_x, end_y);
          wire_area += defDist(abs(end_x - start_x) + abs(end_y - start_y))
                       * wire_width;
          side_wire_area
              += (defDist(abs(end_x - start_x) + abs(end_y - start_y))
                  + wire_width)
                 * defDist(wire_thickness_uint) * 2;
        }

        std::pair<double, double> areas
            = calculateWireArea(edge->source(), wire_level, nv, level_nodes);
        wire_area += areas.first;
        side_wire_area += areas.second;
      }

      if (edge_io_type.compare("OUT") == 0) {
        if (node->layer()->getRoutingLevel() == wire_level) {
          level_nodes.insert(node);
          edge->target()->xy(end_x, end_y);
          wire_area += defDist(abs(end_x - start_x) + abs(end_y - start_y))
                       * wire_width;
          side_wire_area
              += (defDist(abs(end_x - start_x) + abs(end_y - start_y))
                  + wire_width)
                 * defDist(wire_thickness_uint) * 2;
        }

        std::pair<double, double> areas
            = calculateWireArea(edge->target(), wire_level, nv, level_nodes);
        wire_area += areas.first;
        side_wire_area += areas.second;
      }
    }
  }
  return {wire_area, side_wire_area};
}

double AntennaChecker::getViaArea(dbWireGraph::Edge* edge)
{
  double via_area = 0.0;
  if (edge->type() == dbWireGraph::Edge::Type::TECH_VIA) {
    dbWireGraph::TechVia* tech_via_edge = (dbWireGraph::TechVia*) edge;
    dbTechVia* tech_via = tech_via_edge->via();
    dbSet<dbBox> tech_via_boxes = tech_via->getBoxes();
    dbSet<dbBox>::iterator box_itr;
    for (box_itr = tech_via_boxes.begin(); box_itr != tech_via_boxes.end();
         ++box_itr) {
      dbBox* box = *box_itr;
      if (box->getTechLayer()->getType() == dbTechLayerType::CUT) {
        uint dx = box->getDX();
        uint dy = box->getDY();
        via_area = defDist(dx) * defDist(dy);
      }
    }
  } else if (edge->type() == dbWireGraph::Edge::Type::VIA) {
    dbWireGraph::Via* via_edge = (dbWireGraph::Via*) edge;
    dbVia* via = via_edge->via();
    dbSet<dbBox> via_boxes = via->getBoxes();
    dbSet<dbBox>::iterator box_itr;
    for (box_itr = via_boxes.begin(); box_itr != via_boxes.end(); ++box_itr) {
      dbBox* box = *box_itr;
      if (box->getTechLayer()->getType() == dbTechLayerType::CUT) {
        uint dx = box->getDX();
        uint dy = box->getDY();
        via_area = defDist(dx) * defDist(dy);
      }
    }
  }
  return via_area;
}

dbTechLayer* AntennaChecker::getViaLayer(dbWireGraph::Edge* edge)
{
  if (edge->type() == dbWireGraph::Edge::Type::TECH_VIA) {
    dbWireGraph::TechVia* tech_via_edge = (dbWireGraph::TechVia*) edge;
    dbTechVia* tech_via = tech_via_edge->via();
    dbSet<dbBox> tech_via_boxes = tech_via->getBoxes();
    dbSet<dbBox>::iterator box_itr;
    for (box_itr = tech_via_boxes.begin(); box_itr != tech_via_boxes.end();
         ++box_itr) {
      dbBox* box = *box_itr;
      if (box->getTechLayer()->getType() == dbTechLayerType::CUT)
        return box->getTechLayer();
    }
  } else if (edge->type() == dbWireGraph::Edge::Type::VIA) {
    dbWireGraph::Via* via_edge = (dbWireGraph::Via*) edge;
    dbVia* via = via_edge->via();
    dbSet<dbBox> via_boxes = via->getBoxes();
    dbSet<dbBox>::iterator box_itr;
    for (box_itr = via_boxes.begin(); box_itr != via_boxes.end(); ++box_itr) {
      dbBox* box = *box_itr;
      if (box->getTechLayer()->getType() == dbTechLayerType::CUT)
        return box->getTechLayer();
    }
  }
  return nullptr;
}

std::string AntennaChecker::getViaName(dbWireGraph::Edge* edge)
{
  if (edge->type() == dbWireGraph::Edge::Type::TECH_VIA) {
    dbWireGraph::TechVia* tech_via_edge = (dbWireGraph::TechVia*) edge;
    dbTechVia* tech_via = tech_via_edge->via();
    return tech_via->getName();
  } else if (edge->type() == dbWireGraph::Edge::Type::VIA) {
    dbWireGraph::Via* via_edge = (dbWireGraph::Via*) edge;
    dbVia* via = via_edge->via();
    return via->getName();
  }
  return nullptr;
}

double AntennaChecker::calculateViaArea(dbWireGraph::Node* node, int wire_level)
{
  double via_area = 0.0;
  if (node->in_edge()
      && (node->in_edge()->type() == dbWireGraph::Edge::Type::VIA
          || node->in_edge()->type() == dbWireGraph::Edge::Type::TECH_VIA))
    if (node->in_edge()->source()->layer()->getRoutingLevel() > wire_level)
      via_area = via_area + getViaArea(node->in_edge());

  dbWireGraph::Node::edge_iterator edge_itr;
  for (edge_itr = node->begin(); edge_itr != node->end(); ++edge_itr) {
    if ((*edge_itr)->type() == dbWireGraph::Edge::Type::SEGMENT
        || (*edge_itr)->type() == dbWireGraph::Edge::Type::SHORT) {
      via_area = via_area + calculateViaArea((*edge_itr)->target(), wire_level);
    } else if ((*edge_itr)->type() == dbWireGraph::Edge::Type::VIA
               || (*edge_itr)->type() == dbWireGraph::Edge::Type::TECH_VIA) {
      if ((*edge_itr)->target()->layer()->getRoutingLevel() > wire_level) {
        via_area = via_area + getViaArea((*edge_itr));
      } else {
        via_area
            = via_area + calculateViaArea((*edge_itr)->target(), wire_level);
      }
    }
  }
  return via_area;
}

dbWireGraph::Edge* AntennaChecker::findVia(dbWireGraph::Node* node,
                                           int wire_level)
{
  if (node->in_edge()
      && (node->in_edge()->type() == dbWireGraph::Edge::Type::VIA
          || node->in_edge()->type() == dbWireGraph::Edge::Type::TECH_VIA))
    if (node->in_edge()->source()->layer()->getRoutingLevel() > wire_level) {
      return node->in_edge();
    }
  dbWireGraph::Node::edge_iterator edge_itr;
  for (edge_itr = node->begin(); edge_itr != node->end(); ++edge_itr) {
    if ((*edge_itr)->type() == dbWireGraph::Edge::Type::SEGMENT
        || (*edge_itr)->type() == dbWireGraph::Edge::Type::SHORT) {
      dbWireGraph::Edge* via = findVia((*edge_itr)->target(), wire_level);
      if (via)
        return via;
    } else if ((*edge_itr)->type() == dbWireGraph::Edge::Type::VIA
               || (*edge_itr)->type() == dbWireGraph::Edge::Type::TECH_VIA) {
      if ((*edge_itr)->target()->layer()->getRoutingLevel() > wire_level) {
        return (*edge_itr);
      } else {
        dbWireGraph::Edge* via = findVia((*edge_itr)->target(), wire_level);
        if (via)
          return via;
      }
    }
  }
  return nullptr;
}

void AntennaChecker::findCarPath(dbWireGraph::Node* node,
                                 int wire_level,
                                 dbWireGraph::Node* goal,
                                 std::vector<dbWireGraph::Node*>& current_path,
                                 std::vector<dbWireGraph::Node*>& path_found)
{
  current_path.push_back(node);

  if (node == goal) {
    std::vector<dbWireGraph::Node*>::iterator current_itr;
    std::vector<dbWireGraph::Node*>::iterator found_itr;

    for (current_itr = current_path.begin(); current_itr != current_path.end();
         ++current_itr) {
      bool existed_node = 0;
      for (found_itr = path_found.begin(); found_itr != path_found.end();
           ++found_itr) {
        if ((*current_itr) == (*found_itr)) {
          existed_node = 1;
          break;
        }
      }
      if (existed_node == 0)
        path_found.push_back((*current_itr));
    }
  } else {
    if (node->in_edge()
        && (node->in_edge()->type() == dbWireGraph::Edge::Type::VIA
            || node->in_edge()->type() == dbWireGraph::Edge::Type::TECH_VIA))
      if (node->in_edge()->source()->layer()->getRoutingLevel()
          < node->in_edge()->target()->layer()->getRoutingLevel()) {
        auto root_info = findSegmentRoot(
            node->in_edge()->source(),
            node->in_edge()->source()->layer()->getRoutingLevel());
        findCarPath(root_info,
                    node->in_edge()->source()->layer()->getRoutingLevel(),
                    goal,
                    current_path,
                    path_found);
      }
    dbWireGraph::Node::edge_iterator edge_itr;
    for (edge_itr = node->begin(); edge_itr != node->end(); ++edge_itr) {
      if ((*edge_itr)->type() == dbWireGraph::Edge::Type::VIA
          || (*edge_itr)->type() == dbWireGraph::Edge::Type::TECH_VIA) {
        if ((*edge_itr)->target()->layer()->getRoutingLevel() <= wire_level)
          findCarPath(findSegmentStart((*edge_itr)->target()),
                      wire_level,
                      goal,
                      current_path,
                      path_found);
      } else if ((*edge_itr)->type() == dbWireGraph::Edge::Type::SEGMENT
                 || (*edge_itr)->type() == dbWireGraph::Edge::Type::SHORT)
        findCarPath(
            (*edge_itr)->target(), wire_level, goal, current_path, path_found);
    }
  }
  current_path.pop_back();
}

void AntennaChecker::buildWireParTable(
    std::vector<PARinfo>& PARtable,
    std::vector<dbWireGraph::Node*> wireroots_info)
{
  std::set<dbWireGraph::Node*> level_nodes = {};
  for (auto root_itr = wireroots_info.begin(); root_itr != wireroots_info.end();
       ++root_itr) {
    dbWireGraph::Node* wireroot = *root_itr;

    if (level_nodes.find(wireroot) != level_nodes.end())
      continue;

    std::set<dbWireGraph::Node*> nv = {};
    std::pair<double, double> areas = calculateWireArea(
        wireroot, wireroot->layer()->getRoutingLevel(), nv, level_nodes);

    double wire_area = areas.first;
    double side_wire_area = areas.second;
    double iterm_areas[2] = {0.0, 0.0};
    std::set<dbITerm*> iv = {};
    nv.clear();

    findWireBelowIterms(
        wireroot, iterm_areas, wireroot->layer()->getRoutingLevel(), iv, nv);

    PARinfo new_par = {*root_itr,
                       iv,
                       wire_area,
                       side_wire_area,
                       {iterm_areas[0], iterm_areas[1]},
                       0.0,
                       0.0,
                       0.0,
                       0.0};
    PARtable.push_back(new_par);
  }

  for (PARinfo& par_info : PARtable)
    calculateParInfo(par_info);
}

bool AntennaChecker::checkIterm(dbWireGraph::Node* node, double iterm_areas[2])
{
  if (node->object() && node->object()->getObjectType() == dbITermObj) {
    dbITerm* iterm = dbITerm::getITerm(db_->getChip()->getBlock(),
                                       node->object()->getId());
    dbMTerm* mterm = iterm->getMTerm();
    std::string inst_name = iterm->getInst()->getConstName();

    if (mterm->hasDefaultAntennaModel()) {
      dbTechAntennaPinModel* pinmodel = mterm->getDefaultAntennaModel();
      std::vector<std::pair<double, dbTechLayer*>> gate_area;
      pinmodel->getGateArea(gate_area);

      std::vector<std::pair<double, dbTechLayer*>>::iterator gate_area_iter;
      double max_gate_area = 0;
      for (gate_area_iter = gate_area.begin();
           gate_area_iter != gate_area.end();
           gate_area_iter++) {
        max_gate_area = std::max(max_gate_area, (*gate_area_iter).first);
      }
      iterm_areas[0] = iterm_areas[0] + max_gate_area;
    }

    std::vector<std::pair<double, dbTechLayer*>> diff_area;
    mterm->getDiffArea(diff_area);

    std::vector<std::pair<double, dbTechLayer*>>::iterator diff_area_iter;
    double max_diff_area = 0;
    for (diff_area_iter = diff_area.begin(); diff_area_iter != diff_area.end();
         diff_area_iter++) {
      max_diff_area = std::max(max_diff_area, (*diff_area_iter).first);
    }
    iterm_areas[1] = iterm_areas[1] + max_diff_area;
    return true;
  } else
    return false;
}

double AntennaChecker::getPwlFactor(dbTechLayerAntennaRule::pwl_pair pwl_info,
                                    double ref_val,
                                    double def)
{
  if (pwl_info.indices.size() != 0) {
    std::vector<double>::const_iterator indice_itr;
    std::vector<double>::const_iterator ratio_itr;
    if (pwl_info.indices.size() == 1) {
      indice_itr = pwl_info.indices.begin();
      ratio_itr = pwl_info.ratios.begin();
      return *ratio_itr;
    } else {
      double pwl_info_indice = 0.0;
      double pwl_info_ratio = 0.0;
      double slope_factor = 1.0;
      for (indice_itr = pwl_info.indices.begin(),
          ratio_itr = pwl_info.ratios.begin();
           indice_itr != pwl_info.indices.end()
           && ratio_itr != pwl_info.ratios.end();
           indice_itr++, ratio_itr++) {
        if (indice_itr == pwl_info.indices.begin()
            && ratio_itr == pwl_info.ratios.begin()) {
          pwl_info_indice = (*indice_itr);
          pwl_info_ratio = (*ratio_itr);
        }

        slope_factor = ((*ratio_itr) - pwl_info_ratio)
                       / ((*indice_itr) - pwl_info_indice);

        if (ref_val >= pwl_info_indice && ref_val < (*indice_itr)) {
          return slope_factor * (ref_val - pwl_info_indice) + pwl_info_ratio;
        } else {
          pwl_info_indice = (*indice_itr);
          pwl_info_ratio = (*ratio_itr);
        }
      }
      return slope_factor * (ref_val - pwl_info_indice) + pwl_info_ratio;
    }
  } else
    return def;

  return def;
}

void AntennaChecker::calculateParInfo(PARinfo& it)
{
  dbWireGraph::Node* wireroot = it.WirerootNode;
  odb::dbTechLayer* tech_layer = wireroot->layer();
  ANTENNAmodel am = layer_info[tech_layer];

  double metal_factor = am.metal_factor;
  double diff_metal_factor = am.diff_metal_factor;
  double side_metal_factor = am.side_metal_factor;
  double diff_side_metal_factor = am.diff_side_metal_factor;

  double minus_diff_factor = am.minus_diff_factor;
  double plus_diff_factor = am.plus_diff_factor;

  double diff_metal_reduce_factor = am.diff_metal_reduce_factor;

  if (tech_layer->hasDefaultAntennaRule()) {
    dbTechLayerAntennaRule* antenna_rule = tech_layer->getDefaultAntennaRule();
    diff_metal_reduce_factor = getPwlFactor(
        antenna_rule->getAreaDiffReduce(), it.iterm_areas[1], 1.0);
  }

  if (it.iterm_areas[0] == 0)
    return;

  if (it.iterm_areas[1] != 0) {
    it.PAR_value = (diff_metal_factor * it.wire_area) / it.iterm_areas[0];
    it.PSR_value
        = (diff_side_metal_factor * it.side_wire_area) / it.iterm_areas[0];
    it.diff_PAR_value
        = (diff_metal_factor * it.wire_area * diff_metal_reduce_factor
           - minus_diff_factor * it.iterm_areas[1])
          / (it.iterm_areas[0] + plus_diff_factor * it.iterm_areas[1]);
    it.diff_PSR_value
        = (diff_side_metal_factor * it.side_wire_area * diff_metal_reduce_factor
           - minus_diff_factor * it.iterm_areas[1])
          / (it.iterm_areas[0] + plus_diff_factor * it.iterm_areas[1]);
  } else {
    it.PAR_value = (metal_factor * it.wire_area) / it.iterm_areas[0];
    it.PSR_value = (side_metal_factor * it.side_wire_area) / it.iterm_areas[0];
    it.diff_PAR_value = (metal_factor * it.wire_area * diff_metal_reduce_factor)
                        / it.iterm_areas[0];
    it.diff_PSR_value
        = (side_metal_factor * it.side_wire_area * diff_metal_reduce_factor)
          / (it.iterm_areas[0]);
  }

  // int x, y;
  // wireroot->xy(x, y);
  // fprintf(_out, "Wireroot - (%d, %d) - %s - wire area - %f - side wire area -
  // %f - iterm areas - %f, %f - par - %f - psr - %f\n", x, y,
  // tech_layer->getConstName(), it.wire_area, it.side_wire_area,
  // it.iterm_areas[0], it.iterm_areas[1], it.PAR_value, it.PSR_value);
}

void AntennaChecker::buildWireCarTable(
    std::vector<ARinfo>& CARtable,
    std::vector<PARinfo> PARtable,
    std::vector<PARinfo> VIA_PARtable,
    std::vector<dbWireGraph::Node*> gate_iterms)
{
  std::vector<dbWireGraph::Node*>::iterator gate_itr;
  for (gate_itr = gate_iterms.begin(); gate_itr != gate_iterms.end();
       ++gate_itr) {
    dbWireGraph::Node* gate = *gate_itr;
    std::vector<PARinfo>::iterator ar_itr;

    for (ar_itr = PARtable.begin(); ar_itr != PARtable.end(); ++ar_itr) {
      dbWireGraph::Node* wireroot = ar_itr->WirerootNode;
      double par = ar_itr->PAR_value;
      double psr = ar_itr->PSR_value;
      double diff_par = ar_itr->diff_PAR_value;
      double diff_psr = ar_itr->diff_PSR_value;
      double diff_area = ar_itr->iterm_areas[1];
      double car = 0.0;
      double csr = 0.0;
      double diff_car = 0.0;
      double diff_csr = 0.0;
      std::vector<dbWireGraph::Node*> current_path;
      std::vector<dbWireGraph::Node*> path_found;
      std::vector<dbWireGraph::Node*> car_wireroots;

      findCarPath(wireroot,
                  wireroot->layer()->getRoutingLevel(),
                  gate,
                  current_path,
                  path_found);
      if (!path_found.empty()) {
        std::vector<dbWireGraph::Node*>::iterator path_itr;
        for (path_itr = path_found.begin(); path_itr != path_found.end();
             ++path_itr) {
          dbWireGraph::Node* node = *path_itr;
          if (ifSegmentRoot(node, node->layer()->getRoutingLevel()))
            car_wireroots.push_back(node);
        }

        std::vector<dbWireGraph::Node*>::iterator car_root_itr;
        for (car_root_itr = car_wireroots.begin();
             car_root_itr != car_wireroots.end();
             ++car_root_itr) {
          dbWireGraph::Node* car_root = *car_root_itr;
          std::vector<PARinfo>::iterator par_itr;
          for (par_itr = PARtable.begin(); par_itr != PARtable.end();
               ++par_itr) {
            if (par_itr->WirerootNode == car_root) {
              car = car + par_itr->PAR_value;
              csr = csr + par_itr->PSR_value;
              diff_car = diff_car + par_itr->diff_PAR_value;
              diff_csr = diff_csr + par_itr->diff_PSR_value;
              break;
            }
          }
          dbTechLayer* wire_layer = wireroot->layer();
          if (wire_layer->hasDefaultAntennaRule()) {
            dbTechLayerAntennaRule* antenna_rule
                = wire_layer->getDefaultAntennaRule();
            if (antenna_rule->hasAntennaCumRoutingPlusCut()) {
              if (car_root->layer()->getRoutingLevel()
                  < wireroot->layer()->getRoutingLevel()) {
                std::vector<PARinfo>::iterator via_par_itr;
                for (via_par_itr = VIA_PARtable.begin();
                     via_par_itr != VIA_PARtable.end();
                     ++via_par_itr) {
                  if (via_par_itr->WirerootNode == car_root) {
                    car = car + via_par_itr->PAR_value;
                    diff_car = diff_car + via_par_itr->diff_PAR_value;
                    break;
                  }
                }
              }
            }
          }
        }

        ARinfo new_car = {wireroot,
                          gate,
                          false,
                          par,
                          psr,
                          diff_par,
                          diff_psr,
                          car,
                          csr,
                          diff_car,
                          diff_csr,
                          diff_area};
        CARtable.push_back(new_car);
      }
    }
  }
}

void AntennaChecker::buildViaParTable(
    std::vector<PARinfo>& VIA_PARtable,
    std::vector<dbWireGraph::Node*> wireroots_info)
{
  for (auto root_itr = wireroots_info.begin(); root_itr != wireroots_info.end();
       ++root_itr) {
    dbWireGraph::Node* wireroot = *root_itr;
    double via_area
        = calculateViaArea(wireroot, wireroot->layer()->getRoutingLevel());
    double iterm_areas[2] = {0.0, 0.0};
    std::set<dbITerm*> iv;
    std::set<dbWireGraph::Node*> nv;
    findWireBelowIterms(
        wireroot, iterm_areas, wireroot->layer()->getRoutingLevel(), iv, nv);
    double par = 0.0;
    double diff_par = 0.0;

    double cut_factor = 1.0;
    double diff_cut_factor = 1.0;

    double minus_diff_factor = 0.0;
    double plus_diff_factor = 0.0;
    double diff_metal_reduce_factor = 1.0;

    if (via_area != 0 && iterm_areas[0] != 0) {
      dbTechLayer* layer = getViaLayer(
          findVia(wireroot, wireroot->layer()->getRoutingLevel()));

      ANTENNAmodel am = layer_info[layer];
      minus_diff_factor = am.minus_diff_factor;
      plus_diff_factor = am.plus_diff_factor;
      diff_metal_reduce_factor = am.diff_metal_reduce_factor;
      if (layer->hasDefaultAntennaRule()) {
        dbTechLayerAntennaRule* antenna_rule = layer->getDefaultAntennaRule();
        diff_metal_reduce_factor = getPwlFactor(
            antenna_rule->getAreaDiffReduce(), iterm_areas[1], 1.0);
      }

      cut_factor = am.cut_factor;
      diff_cut_factor = am.diff_cut_factor;

      minus_diff_factor = am.minus_diff_factor;
      plus_diff_factor = am.plus_diff_factor;

      if (iterm_areas[1] != 0) {
        par = (diff_cut_factor * via_area) / iterm_areas[0];
        diff_par = (diff_cut_factor * via_area * diff_metal_reduce_factor
                    - minus_diff_factor * iterm_areas[1])
                   / (iterm_areas[0] + plus_diff_factor * iterm_areas[1]);
      } else {
        par = (cut_factor * via_area) / iterm_areas[0];
        diff_par = (cut_factor * via_area * diff_metal_reduce_factor
                    - minus_diff_factor * iterm_areas[1])
                   / (iterm_areas[0] + plus_diff_factor * iterm_areas[1]);
      }
      PARinfo new_par
          = {*root_itr, iv, 0.0, 0.0, {0.0, 0.0}, par, 0.0, diff_par, 0.0};
      VIA_PARtable.push_back(new_par);

      // int x, y;
      // wireroot->xy(x, y);
      // fprintf(_out, "VIA - (%d, %d) - via area - %f - iterm areas - (%f, %f)
      // - par -%f - diff par - %f\n", x, y, via_area, iterm_areas[0],
      // iterm_areas[1], par, diff_par);
    }
  }
}

void AntennaChecker::buildViaCarTable(
    std::vector<ARinfo>& VIA_CARtable,
    std::vector<PARinfo> PARtable,
    std::vector<PARinfo> VIA_PARtable,
    std::vector<dbWireGraph::Node*> gate_iterms)
{
  std::vector<dbWireGraph::Node*>::iterator gate_itr;
  for (gate_itr = gate_iterms.begin(); gate_itr != gate_iterms.end();
       ++gate_itr) {
    dbWireGraph::Node* gate = *gate_itr;
    int x, y;
    gate->xy(x, y);

    std::vector<PARinfo>::iterator ar_itr;
    for (ar_itr = VIA_PARtable.begin(); ar_itr != VIA_PARtable.end();
         ++ar_itr) {
      dbWireGraph::Node* wireroot = ar_itr->WirerootNode;
      double par = ar_itr->PAR_value;
      double diff_par = ar_itr->diff_PAR_value;
      double diff_area = ar_itr->iterm_areas[1];
      double car = 0.0;
      double diff_car = 0.0;
      std::vector<dbWireGraph::Node*> current_path;
      std::vector<dbWireGraph::Node*> path_found;
      std::vector<dbWireGraph::Node*> car_wireroots;

      findCarPath(wireroot,
                  wireroot->layer()->getRoutingLevel(),
                  gate,
                  current_path,
                  path_found);
      if (!path_found.empty()) {
        std::vector<dbWireGraph::Node*>::iterator path_itr;
        for (path_itr = path_found.begin(); path_itr != path_found.end();
             ++path_itr) {
          dbWireGraph::Node* node = *path_itr;
          int x, y;
          node->xy(x, y);
          if (ifSegmentRoot(node, node->layer()->getRoutingLevel()))
            car_wireroots.push_back(node);
        }
        std::vector<dbWireGraph::Node*>::iterator car_root_itr;
        for (car_root_itr = car_wireroots.begin();
             car_root_itr != car_wireroots.end();
             ++car_root_itr) {
          dbWireGraph::Node* car_root = *car_root_itr;
          int x, y;
          car_root->xy(x, y);
          std::vector<PARinfo>::iterator par_itr;
          std::vector<PARinfo>::iterator via_par_itr;
          for (via_par_itr = VIA_PARtable.begin();
               via_par_itr != VIA_PARtable.end();
               ++via_par_itr) {
            if (via_par_itr->WirerootNode == car_root) {
              car = car + via_par_itr->PAR_value;
              diff_car = diff_car + via_par_itr->diff_PAR_value;
              break;
            }
          }
          dbTechLayer* via_layer = getViaLayer(
              findVia(wireroot, wireroot->layer()->getRoutingLevel()));
          if (via_layer->hasDefaultAntennaRule()) {
            dbTechLayerAntennaRule* antenna_rule
                = via_layer->getDefaultAntennaRule();
            if (antenna_rule->hasAntennaCumRoutingPlusCut()) {
              for (par_itr = PARtable.begin(); par_itr != PARtable.end();
                   ++par_itr) {
                if (par_itr->WirerootNode == car_root) {
                  car = car + par_itr->PAR_value;
                  diff_car = diff_car + par_itr->diff_PAR_value;
                  break;
                }
              }
            }
          }
        }

        ARinfo new_car = {wireroot,
                          gate,
                          false,
                          par,
                          0.0,
                          diff_par,
                          0.0,
                          car,
                          0.0,
                          diff_car,
                          0.0,
                          diff_area};
        VIA_CARtable.push_back(new_car);
      }
    }
  }
}

std::pair<bool, bool> AntennaChecker::checkWirePar(ARinfo AntennaRatio,
                                                   bool report_violating_nets,
                                                   bool print)
{
  dbTechLayer* layer = AntennaRatio.WirerootNode->layer();
  double par = AntennaRatio.PAR_value;
  double psr = AntennaRatio.PSR_value;
  double diff_par = AntennaRatio.diff_PAR_value;
  double diff_psr = AntennaRatio.diff_PSR_value;
  double diff_area = AntennaRatio.diff_area;

  bool checked = false;
  bool if_violated = false;

  bool par_violation = false;
  bool diff_par_violation = false;
  bool psr_violation = false;
  bool diff_psr_violation = false;

  if (layer->hasDefaultAntennaRule()) {
    // pre-check if there are violations, in case a simple report is required
    dbTechLayerAntennaRule* antenna_rule = layer->getDefaultAntennaRule();

    double PAR_ratio = antenna_rule->getPAR();
    dbTechLayerAntennaRule::pwl_pair diffPAR = antenna_rule->getDiffPAR();
    double diffPAR_PWL_ratio = getPwlFactor(diffPAR, diff_area, 0);

    if (PAR_ratio != 0) {
      if (par > PAR_ratio) {
        par_violation = true;
        if_violated = true;
      }
    } else {
      if (diffPAR_PWL_ratio != 0) {
        checked = true;
        if (diff_par > diffPAR_PWL_ratio) {
          diff_par_violation = true;
          if_violated = true;
        }
      }
    }

    double PSR_ratio = antenna_rule->getPSR();
    dbTechLayerAntennaRule::pwl_pair diffPSR = antenna_rule->getDiffPSR();
    double diffPSR_PWL_ratio = getPwlFactor(diffPSR, diff_area, 0.0);
    if (PSR_ratio != 0) {
      if (psr > PSR_ratio) {
        psr_violation = true;
        if_violated = true;
      }
    } else {
      if (diffPSR_PWL_ratio != 0) {
        checked = true;
        if (diff_psr > diffPSR_PWL_ratio) {
          diff_psr_violation = true;
          if_violated = true;
          ;
        }
      }
    }

    if (!print) {
      return {if_violated, checked};
    }

    // generate final report, depnding on if report_violating_nets is needed
    if (!if_violated && report_violating_nets)
      return {if_violated, checked};
    else {
      if (report_violating_nets) {
        if (par_violation) {
          fprintf(_out,
                  "  PAR: %7.2f*  Ratio: %7.2f       (Area)\n",
                  par,
                  PAR_ratio);
        } else if (diff_par_violation) {
          fprintf(_out,
                  "  PAR: %7.2f*  Ratio: %7.2f       (Area)\n",
                  diff_par,
                  diffPAR_PWL_ratio);
        } else if (psr_violation) {
          fprintf(_out,
                  "  PAR: %7.2f*  Ratio: %7.2f       (S.Area)\n",
                  psr,
                  PSR_ratio);
        } else {
          fprintf(_out,
                  "  PAR: %7.2f*  Ratio: %7.2f       (S.Area)\n",
                  diff_psr,
                  diffPSR_PWL_ratio);
        }
      } else {
        if (PAR_ratio != 0) {
          fprintf(_out, "  PAR: %7.2f", par);
          if (par_violation) {
            fprintf(_out, "*");
          }
          fprintf(_out, "  Ratio: %7.2f       (Area)\n", PAR_ratio);
        } else {
          fprintf(_out, "  PAR: %7.2f", diff_par);
          if (diffPAR_PWL_ratio == 0)
            fprintf(_out, "  Ratio:    0.00       (Area)\n");
          else {
            if (diff_par_violation) {
              fprintf(_out, "*");
            }
            fprintf(_out, "  Ratio: %7.2f       (Area)\n", diffPAR_PWL_ratio);
          }
        }

        if (PSR_ratio != 0) {
          fprintf(_out, "  PAR: %7.2f", psr);
          if (psr_violation) {
            fprintf(_out, "*");
          }
          fprintf(_out, "  Ratio: %7.2f       (S.Area)\n", PSR_ratio);
        } else {
          fprintf(_out, "  PAR: %7.2f", diff_psr);
          if (diffPSR_PWL_ratio == 0)
            fprintf(_out, "  Ratio:    0.00       (S.Area)\n");
          else {
            if (diff_psr_violation) {
              fprintf(_out, "*");
            }
            fprintf(_out, "  Ratio: %7.2f       (S.Area)\n", diffPSR_PWL_ratio);
          }
        }
      }
    }
  }

  return {if_violated, checked};
}

std::pair<bool, bool> AntennaChecker::checkWireCar(ARinfo AntennaRatio,
                                                   bool par_checked,
                                                   bool report_violating_nets,
                                                   bool print)
{
  dbTechLayer* layer = AntennaRatio.WirerootNode->layer();
  double car = AntennaRatio.CAR_value;
  double csr = AntennaRatio.CSR_value;
  double diff_car = AntennaRatio.diff_CAR_value;
  double diff_csr = AntennaRatio.diff_CSR_value;
  double diff_area = AntennaRatio.diff_area;

  bool checked = 0;
  bool if_violated = 0;

  bool car_violation = false;
  bool diff_car_violation = false;
  bool csr_violation = false;
  bool diff_csr_violation = false;

  if (layer->hasDefaultAntennaRule()) {
    dbTechLayerAntennaRule* antenna_rule = layer->getDefaultAntennaRule();

    double CAR_ratio = par_checked ? 0.0 : antenna_rule->getCAR();
    dbTechLayerAntennaRule::pwl_pair diffCAR = antenna_rule->getDiffCAR();
    double diffCAR_PWL_ratio
        = par_checked ? 0.0 : getPwlFactor(diffCAR, diff_area, 0);
    if (CAR_ratio != 0) {
      if (car > CAR_ratio) {
        car_violation = true;
        if_violated = true;
      }
    } else {
      if (diffCAR_PWL_ratio != 0) {
        checked = true;
        if (car > diffCAR_PWL_ratio) {
          diff_car_violation = true;
          if_violated = true;
        }
      }
    }

    double CSR_ratio = par_checked ? 0.0 : antenna_rule->getCSR();
    dbTechLayerAntennaRule::pwl_pair diffCSR = antenna_rule->getDiffCSR();
    double diffCSR_PWL_ratio
        = par_checked ? 0.0 : getPwlFactor(diffCSR, diff_area, 0.0);
    if (CSR_ratio != 0) {
      if (csr > CSR_ratio) {
        csr_violation = true;
        if_violated = true;
      }
    } else {
      if (diffCSR_PWL_ratio != 0) {
        checked = true;
        if (diff_csr > diffCSR_PWL_ratio) {
          diff_csr_violation = true;
          if_violated = true;
        }
      }
    }

    if (!print) {
      return {if_violated, checked};
    }

    if (!if_violated && report_violating_nets) {
      return {if_violated, checked};
    } else {
      if (report_violating_nets) {
        if (car_violation) {
          fprintf(_out,
                  "  CAR: %7.2f*  Ratio: %7.2f       (Area)\n",
                  car,
                  CAR_ratio);
        } else if (diff_car_violation) {
          fprintf(_out,
                  "  CAR: %7.2f*  Ratio: %7.2f       (Area)\n",
                  diff_car,
                  diffCAR_PWL_ratio);
        } else if (csr_violation) {
          fprintf(_out,
                  "  CAR: %7.2f*  Ratio: %7.2f       (C.S.Area)\n",
                  csr,
                  CSR_ratio);
        } else {
          fprintf(_out,
                  "  CAR: %7.2f*  Ratio: %7.2f       (C.S.Area)\n",
                  diff_csr,
                  diffCSR_PWL_ratio);
        }
      } else {
        if (CAR_ratio != 0) {
          fprintf(_out, "  CAR: %7.2f", car);
          if (car_violation) {
            fprintf(_out, "*");
          }
          fprintf(_out, "  Ratio: %7.2f       (C.Area)\n", CAR_ratio);
        } else {
          fprintf(_out, "  CAR: %7.2f", car);
          if (diffCAR_PWL_ratio == 0)
            fprintf(_out, "  Ratio:    0.00       (C.Area)\n");
          else {
            if (diff_car_violation) {
              fprintf(_out, "*");
            }
            fprintf(_out, "  Ratio: %7.2f       (C.Area)\n", diffCAR_PWL_ratio);
          }
        }

        if (CSR_ratio != 0) {
          fprintf(_out, "  CAR: %7.2f", csr);
          if (csr_violation) {
            fprintf(_out, "*");
          }
          fprintf(_out, "  Ratio: %7.2f       (C.S.Area)\n", CSR_ratio);
        } else {
          fprintf(_out, "  CAR: %7.2f", diff_csr);
          if (diffCSR_PWL_ratio == 0)
            fprintf(_out, "  Ratio:    0.00       (C.S.Area)\n");
          else {
            if (diff_csr_violation) {
              fprintf(_out, "*");
            }
            fprintf(
                _out, "  Ratio: %7.2f       (C.S.Area)\n", diffCSR_PWL_ratio);
          }
        }
      }
    }
  }
  return {if_violated, checked};
}

bool AntennaChecker::checkViaPar(ARinfo AntennaRatio,
                                 bool report_violating_nets,
                                 bool print)
{
  dbTechLayer* layer = getViaLayer(
      findVia(AntennaRatio.WirerootNode,
              AntennaRatio.WirerootNode->layer()->getRoutingLevel()));
  double par = AntennaRatio.PAR_value;
  double diff_par = AntennaRatio.diff_PAR_value;
  double diff_area = AntennaRatio.diff_area;

  bool if_violated = 0;

  bool par_violation = false;
  bool diff_par_violation = false;

  if (layer->hasDefaultAntennaRule()) {
    dbTechLayerAntennaRule* antenna_rule = layer->getDefaultAntennaRule();
    double PAR_ratio = antenna_rule->getPAR();

    dbTechLayerAntennaRule::pwl_pair diffPAR = antenna_rule->getDiffPAR();
    double diffPAR_PWL_ratio = getPwlFactor(diffPAR, diff_area, 0);
    if (PAR_ratio != 0) {
      if (par > PAR_ratio) {
        par_violation = true;
        if_violated = true;
      }
    } else {
      if (diffPAR_PWL_ratio != 0) {
        if (diff_par > diffPAR_PWL_ratio) {
          diff_par_violation = true;
          if_violated = true;
        }
      }
    }

    if (!print) {
      return if_violated;
    }

    if (!if_violated && report_violating_nets) {
      return false;
    } else {
      if (report_violating_nets) {
        if (par_violation) {
          fprintf(_out,
                  "  PAR: %7.2f*  Ratio: %7.2f       (Area)\n",
                  par,
                  PAR_ratio);
        } else {
          fprintf(_out,
                  "  PAR: %7.2f*  Ratio: %7.2f       (Area)\n",
                  par,
                  diffPAR_PWL_ratio);
        }
      } else {
        if (PAR_ratio != 0) {
          fprintf(_out, "  PAR: %7.2f", par);
          if (par_violation) {
            fprintf(_out, "*");
          }
          fprintf(_out, "  Ratio: %7.2f       (Area)\n", PAR_ratio);
        } else {
          fprintf(_out, "  PAR: %7.2f", par);
          if (diffPAR_PWL_ratio == 0)
            fprintf(_out, "  Ratio:    0.00       (Area)\n");
          else {
            if (diff_par_violation) {
              fprintf(_out, "*");
            }
            fprintf(_out, "  Ratio: %7.2f       (Area)\n", diffPAR_PWL_ratio);
          }
        }
      }
    }
  }
  return if_violated;
}

bool AntennaChecker::checkViaCar(ARinfo AntennaRatio,
                                 bool report_violating_nets,
                                 bool print)
{
  dbTechLayer* layer = getViaLayer(
      findVia(AntennaRatio.WirerootNode,
              AntennaRatio.WirerootNode->layer()->getRoutingLevel()));
  double car = AntennaRatio.CAR_value;
  double diff_area = AntennaRatio.diff_area;

  bool if_violated = 0;

  bool car_violation = false;
  bool diff_car_violation = false;

  if (layer->hasDefaultAntennaRule()) {
    dbTechLayerAntennaRule* antenna_rule = layer->getDefaultAntennaRule();
    double CAR_ratio = antenna_rule->getCAR();

    dbTechLayerAntennaRule::pwl_pair diffCAR = antenna_rule->getDiffCAR();
    double diffCAR_PWL_ratio = getPwlFactor(diffCAR, diff_area, 0);

    if (CAR_ratio != 0) {
      if (car > CAR_ratio) {
        car_violation = true;
        if_violated = true;
      }
    } else {
      if (diffCAR_PWL_ratio != 0) {
        if (car > diffCAR_PWL_ratio) {
          diff_car_violation = true;
          if_violated = true;
        }
      }
    }

    if (!print) {
      return if_violated;
    }

    if (!if_violated && report_violating_nets) {
      return false;
    } else {
      if (report_violating_nets) {
        if (car_violation) {
          fprintf(_out,
                  "  CAR: %7.2f*  Ratio: %7.2f       (C.Area)\n",
                  car,
                  CAR_ratio);
        } else {
          fprintf(_out,
                  "  CAR: %7.2f*  Ratio: %7.2f       (C.Area)\n",
                  car,
                  diffCAR_PWL_ratio);
        }
      } else {
        if (CAR_ratio != 0) {
          fprintf(_out, "  CAR: %7.2f", car);
          if (car_violation) {
            fprintf(_out, "*");
          }
          fprintf(_out, "  Ratio: %7.2f       (C.Area)\n", CAR_ratio);
        } else {
          fprintf(_out, "  CAR: %7.2f", car);
          if (diffCAR_PWL_ratio == 0)
            fprintf(_out, "  Ratio:    0.00       (C.Area)\n");
          else {
            if (diff_car_violation) {
              fprintf(_out, "*");
            }
            fprintf(_out, "  Ratio: %7.2f       (C.Area)\n", diffCAR_PWL_ratio);
          }
        }
      }
    }
  }
  return if_violated;
}

std::vector<int> AntennaChecker::getAntennaRatio(std::string report_filename,
                                                 bool report_violating_nets)
{
  std::string bname = db_->getChip()->getBlock()->getName();

  _out = fopen(report_filename.c_str(), "w");
  if (_out) {
    checkAntennaCell();

    dbSet<dbNet> nets = db_->getChip()->getBlock()->getNets();
    if (nets.size() == 0)
      return {0, 0, 0};

    dbSet<dbNet>::iterator net_itr;
    int num_total_net = 0;
    int num_violated_net = 0;
    int num_violated_pins = 0;
    for (net_itr = nets.begin(); net_itr != nets.end(); ++net_itr) {
      dbNet* net = *net_itr;

      if (net->isSpecial())
        continue;
      num_total_net++;
      std::string nname = net->getConstName();
      dbWire* wire = net->getWire();
      dbWireGraph graph;
      if (wire) {
        graph.decode(wire);
        dbWireGraph::node_iterator node_itr;
        dbWireGraph::edge_iterator edge_itr;

        std::vector<dbWireGraph::Node*> wireroots_info;
        std::vector<dbWireGraph::Node*> gate_iterms;

        for (node_itr = graph.begin_nodes(); node_itr != graph.end_nodes();
             ++node_itr) {
          dbWireGraph::Node* node = *node_itr;

          auto wireroot_info
              = findSegmentRoot(node, node->layer()->getRoutingLevel());
          dbWireGraph::Node* wireroot = wireroot_info;

          if (wireroot) {
            bool find_root = 0;
            for (auto root_itr = wireroots_info.begin();
                 root_itr != wireroots_info.end();
                 ++root_itr) {
              if (find_root)
                break;
              else {
                if (*root_itr == wireroot)
                  find_root = 1;
              }
            }
            if (!find_root) {
              wireroots_info.push_back(wireroot_info);
            }
          }
          if (node->object()
              && node->object()->getObjectType() == dbITermObj) {
            dbITerm* iterm = dbITerm::getITerm(db_->getChip()->getBlock(),
                                               node->object()->getId());
            dbMTerm* mterm = iterm->getMTerm();
            if (mterm->getIoType() == dbIoType::INPUT)
              if (mterm->hasDefaultAntennaModel())
                gate_iterms.push_back(node);
          }
        }

        if (gate_iterms.size() == 0)
          fprintf(_out, "  No sinks on this net\n");

        std::vector<PARinfo> PARtable;
        buildWireParTable(PARtable, wireroots_info);

        std::vector<PARinfo> VIA_PARtable;
        buildViaParTable(VIA_PARtable, wireroots_info);

        std::vector<ARinfo> CARtable;
        buildWireCarTable(CARtable, PARtable, VIA_PARtable, gate_iterms);

        std::vector<ARinfo> VIA_CARtable;
        buildViaCarTable(VIA_CARtable, PARtable, VIA_PARtable, gate_iterms);

        bool if_violated_wire = 0;
        bool if_violated_VIA = 0;

        std::set<dbWireGraph::Node*> violated_iterms;

        std::vector<dbWireGraph::Node*>::iterator gate_itr;
        bool print_net = true;
        for (gate_itr = gate_iterms.begin(); gate_itr != gate_iterms.end();
             ++gate_itr) {
          dbWireGraph::Node* gate = *gate_itr;

          dbITerm* iterm = dbITerm::getITerm(db_->getChip()->getBlock(),
                                             gate->object()->getId());
          dbMTerm* mterm = iterm->getMTerm();

          bool violation = false;
          unordered_set<dbWireGraph::Node*> violated_gates;

          for (auto ar : CARtable) {
            if (ar.GateNode == gate) {
              auto wire_PAR_violation
                  = checkWirePar(ar, report_violating_nets, false);
              auto wire_CAR_violation = checkWireCar(
                  ar, wire_PAR_violation.second, report_violating_nets, false);
              bool wire_violation
                  = wire_PAR_violation.first || wire_CAR_violation.first;
              violation |= wire_violation;
              if (wire_violation)
                violated_gates.insert(gate);
            }
          }
          for (auto via_ar : VIA_CARtable) {
            if (via_ar.GateNode == gate) {
              bool VIA_PAR_violation
                  = checkViaPar(via_ar, report_violating_nets, false);
              bool VIA_CAR_violation
                  = checkViaCar(via_ar, report_violating_nets, false);
              bool via_violation = VIA_PAR_violation || VIA_CAR_violation;
              violation |= via_violation;
              if (via_violation
                  && (violated_gates.find(gate) == violated_gates.end()))
                violated_gates.insert(gate);
            }
          }

          if ((!report_violating_nets || violation) && print_net) {
            fprintf(_out, "\nNet - %s\n", nname.c_str());
            print_net = false;
          }

          if (!report_violating_nets
              || (violated_gates.find(gate) != violated_gates.end())) {
            fprintf(_out,
                    "  %s  (%s)  %s\n",
                    iterm->getInst()->getConstName(),
                    mterm->getMaster()->getConstName(),
                    mterm->getConstName());
          }

          for (auto ar : CARtable) {
            if (ar.GateNode == gate) {
              auto wire_PAR_violation
                  = checkWirePar(ar, report_violating_nets, false);
              auto wire_CAR_violation = checkWireCar(
                  ar, wire_PAR_violation.second, report_violating_nets, false);
              if (wire_PAR_violation.first || wire_CAR_violation.first
                  || !report_violating_nets) {
                fprintf(_out,
                        "[1]  %s:\n",
                        ar.WirerootNode->layer()->getConstName());
              }
              wire_PAR_violation
                  = checkWirePar(ar, report_violating_nets, true);
              wire_CAR_violation = checkWireCar(
                  ar, wire_PAR_violation.second, report_violating_nets, true);
              if (wire_PAR_violation.first || wire_CAR_violation.first) {
                if_violated_wire = 1;
                if (violated_iterms.find(gate) == violated_iterms.end())
                  violated_iterms.insert(gate);
              }
              if (wire_PAR_violation.first || wire_CAR_violation.first
                  || !report_violating_nets) {
                fprintf(_out, "\n");
              }
            }
          }

          for (auto via_ar : VIA_CARtable) {
            if (via_ar.GateNode == gate) {
              dbWireGraph::Edge* via
                  = findVia(via_ar.WirerootNode,
                            via_ar.WirerootNode->layer()->getRoutingLevel());

              bool VIA_PAR_violation
                  = checkViaPar(via_ar, report_violating_nets, false);
              bool VIA_CAR_violation
                  = checkViaCar(via_ar, report_violating_nets, false);
              if (VIA_PAR_violation || VIA_CAR_violation
                  || !report_violating_nets) {
                fprintf(_out, "[1]  %s:\n", getViaName(via).c_str());
              }
              VIA_PAR_violation
                  = checkViaPar(via_ar, report_violating_nets, true);
              VIA_CAR_violation
                  = checkViaCar(via_ar, report_violating_nets, true);
              if (VIA_PAR_violation || VIA_CAR_violation) {
                if_violated_VIA = 1;
                if (violated_iterms.find(gate) == violated_iterms.end())
                  violated_iterms.insert(gate);
              }
              if (VIA_PAR_violation || VIA_CAR_violation
                  || !report_violating_nets) {
                fprintf(_out, "\n");
              }
            }
          }
        }

        if (if_violated_wire || if_violated_VIA) {
          num_violated_net++;
          num_violated_pins += violated_iterms.size();
        }
      }
    }
    fprintf(_out,
            "Number of pins violated: %d\nNumber of nets violated: %d\nTotal "
            "number of unspecial nets: %d\n",
            num_violated_pins,
            num_violated_net,
            num_total_net);
    return {num_violated_pins, num_violated_net, num_total_net};
  } else {
    logger_->error(
        ANT, 7, "Cannot open report file ({}) for writing", report_filename);
    return {0, 0, 0};
  }
}

void AntennaChecker::checkAntennaCell()
{
  std::vector<dbMaster*> masters;
  db_->getChip()->getBlock()->getMasters(masters);

  for (auto master : masters) {
    dbMasterType type = master->getType();
    if (type == dbMasterType::CORE_ANTENNACELL) {
      dbSet<dbMTerm> mterms = master->getMTerms();

      dbSet<dbMTerm>::iterator mterm_itr;
      double max_diff_area = 0;
      for (mterm_itr = mterms.begin(); mterm_itr != mterms.end(); ++mterm_itr) {
        std::vector<std::pair<double, dbTechLayer*>> diff_areas;
        (*mterm_itr)->getDiffArea(diff_areas);
        for (auto diff_area : diff_areas)
          max_diff_area = std::max(max_diff_area, diff_area.first);
      }

      if (max_diff_area != 0)
        fprintf(_out,
                "Success - antenna cell with diffusion area %f is found\n",
                max_diff_area);
      else
        fprintf(_out,
                "Warning - antenna cell is found but the diffusion area is not "
                "specified\n");

      return;
    }
  }

  fprintf(_out,
          "Warning - class CORE ANTENNACELL is not found. This message can be "
          "ignored if not in the antenna-avoid flow\n");
}

int AntennaChecker::checkAntennas(std::string path, bool report_violating_nets)
{
  odb::dbBlock* block = db_->getChip()->getBlock();
  odb::orderWires(block,
                  nullptr /* net_name_or_id*/,
                  false /* force */,
                  false /* verbose */,
                  true /* quiet */);

  std::string bname = block->getName();
  std::vector<int> nets_info = getAntennaRatio(path, report_violating_nets);
  if (nets_info[2] != 0) {
    logger_->info(ANT, 1, "Found {} pin violations.", nets_info[0]);
    logger_->info(ANT,
                  2,
                  "Found {} net violations in {} nets.",
                  nets_info[1],
                  nets_info[2]);
  }
  return nets_info[1];
}

void AntennaChecker::findWirerootIterms(dbWireGraph::Node* node,
                                        int wire_level,
                                        std::vector<dbITerm*>& gates)
{
  double iterm_areas[2] = {0.0, 0.0};
  std::set<dbITerm*> iv;
  std::set<dbWireGraph::Node*> nv;

  findWireBelowIterms(node, iterm_areas, wire_level, iv, nv);
  gates.assign(iv.begin(), iv.end());
}

std::vector<std::pair<double, std::vector<dbITerm*>>>
AntennaChecker::parMaxWireLength(dbNet* net, int layer)
{
  std::vector<std::pair<double, std::vector<dbITerm*>>> par_wires;
  if (net->isSpecial())
    return par_wires;
  dbWire* wire = net->getWire();
  if (wire != nullptr) {
    dbWireGraph graph;
    graph.decode(wire);

    dbWireGraph::node_iterator node_itr;
    dbWireGraph::edge_iterator edge_itr;

    std::vector<dbWireGraph::Node*> wireroots;
    auto wireroots_info = getWireroots(graph);

    std::set<dbWireGraph::Node*> level_nodes;
    for (auto root_itr : wireroots_info) {
      dbWireGraph::Node* wireroot = root_itr;
      odb::dbTechLayer* tech_layer = wireroot->layer();
      if (level_nodes.find(wireroot) == level_nodes.end()
          && tech_layer->getRoutingLevel() == layer) {
        double max_length = 0;
        std::set<dbWireGraph::Node*> nv;
        std::pair<double, double> areas = calculateWireArea(
            wireroot, tech_layer->getRoutingLevel(), nv, level_nodes);
        double wire_area = areas.first;
        double iterm_areas[2] = {0.0, 0.0};
        std::set<dbITerm*> iv;
        nv.clear();
        findWireBelowIterms(
            wireroot, iterm_areas, tech_layer->getRoutingLevel(), iv, nv);
        double wire_width = defDist(tech_layer->getWidth());

        ANTENNAmodel am = layer_info[tech_layer];
        double metal_factor = am.metal_factor;
        double diff_metal_factor = am.diff_metal_factor;

        double minus_diff_factor = am.minus_diff_factor;
        double plus_diff_factor = am.plus_diff_factor;
        double diff_metal_reduce_factor = am.diff_metal_reduce_factor;

        if (iterm_areas[0] != 0 && tech_layer->hasDefaultAntennaRule()) {
          dbTechLayerAntennaRule* antenna_rule
              = tech_layer->getDefaultAntennaRule();
          dbTechLayerAntennaRule::pwl_pair diff_metal_reduce_factor_pwl
              = antenna_rule->getAreaDiffReduce();
          diff_metal_reduce_factor
              = getPwlFactor(diff_metal_reduce_factor_pwl, iterm_areas[1], 1.0);

          double PAR_ratio = antenna_rule->getPAR();
          if (PAR_ratio != 0) {
            if (iterm_areas[1] != 0)
              max_length
                  = (PAR_ratio * iterm_areas[0] - diff_metal_factor * wire_area)
                    / wire_width;
            else
              max_length
                  = (PAR_ratio * iterm_areas[0] - metal_factor * wire_area)
                    / wire_width;
          } else {
            dbTechLayerAntennaRule::pwl_pair diffPAR
                = antenna_rule->getDiffPAR();
            double diffPAR_ratio = getPwlFactor(diffPAR, iterm_areas[1], 0.0);
            if (iterm_areas[1] != 0)
              max_length
                  = (diffPAR_ratio
                         * (iterm_areas[0] + plus_diff_factor * iterm_areas[1])
                     - (diff_metal_factor * wire_area * diff_metal_reduce_factor
                        - minus_diff_factor * iterm_areas[1]))
                    / wire_width;
            else
              max_length
                  = (diffPAR_ratio
                         * (iterm_areas[0] + plus_diff_factor * iterm_areas[1])
                     - (metal_factor * wire_area * diff_metal_reduce_factor
                        - minus_diff_factor * iterm_areas[1]))
                    / wire_width;
          }
          if (max_length != 0) {
            std::vector<dbITerm*> gates;
            findWirerootIterms(
                wireroot, wireroot->layer()->getRoutingLevel(), gates);
            std::pair<double, std::vector<dbITerm*>> par_wire
                = std::make_pair(max_length, gates);
            par_wires.push_back(par_wire);
          }
        }
      }
    }
  }
  return par_wires;
}

void AntennaChecker::checkMaxLength(const char* net_name, int layer)
{
  std::string bname = db_->getChip()->getBlock()->getName();
  dbNet* net = db_->getChip()->getBlock()->findNet(net_name);
  if (!net->isSpecial()) {
    std::vector<std::pair<double, std::vector<dbITerm*>>> par_max_length_wires
        = parMaxWireLength(net, layer);
    for (auto par_wire : par_max_length_wires) {
      logger_->warn(ANT,
                    3,
                    "Net {}: Routing Level: {}, Max Length for PAR: {:3.2f}",
                    net_name,
                    layer,
                    par_wire.first);
    }
  }
}

std::vector<dbWireGraph::Node*> AntennaChecker::getWireroots(dbWireGraph graph)
{
  std::vector<dbWireGraph::Node*> wireroots_info;
  dbWireGraph::node_iterator node_itr;

  for (node_itr = graph.begin_nodes(); node_itr != graph.end_nodes();
       ++node_itr) {
    dbWireGraph::Node* node = *node_itr;
    auto wireroot_info
        = findSegmentRoot(node, node->layer()->getRoutingLevel());
    dbWireGraph::Node* wireroot = wireroot_info;
    if (wireroot) {
      bool find_root = 0;
      for (auto root_itr = wireroots_info.begin();
           root_itr != wireroots_info.end();
           ++root_itr) {
        if (find_root)
          break;
        else {
          if (*root_itr == wireroot)
            find_root = 1;
        }
      }
      if (!find_root)
        wireroots_info.push_back(wireroot_info);
    }
  }
  return wireroots_info;
}

bool AntennaChecker::checkViolation(PARinfo par_info, dbTechLayer* layer)
{
  double par = par_info.PAR_value;
  double psr = par_info.PSR_value;
  double diff_par = par_info.diff_PAR_value;
  double diff_psr = par_info.diff_PSR_value;
  double diff_area = par_info.iterm_areas[1];

  bool wire_PAR_violation = 0;
  if (layer->hasDefaultAntennaRule()) {
    dbTechLayerAntennaRule* antenna_rule = layer->getDefaultAntennaRule();
    double PAR_ratio = antenna_rule->getPAR();
    if (PAR_ratio != 0) {
      if (par > PAR_ratio)
        wire_PAR_violation = 1;
    } else {
      dbTechLayerAntennaRule::pwl_pair diffPAR = antenna_rule->getDiffPAR();
      double diffPAR_ratio = getPwlFactor(diffPAR, diff_area, 0.0);
      if (diffPAR_ratio != 0 && diff_par > diffPAR_ratio)
        wire_PAR_violation = 1;
    }

    double PSR_ratio = antenna_rule->getPSR();
    if (PSR_ratio != 0) {
      if (psr > PSR_ratio)
        wire_PAR_violation = 1;
    } else {
      dbTechLayerAntennaRule::pwl_pair diffPSR = antenna_rule->getDiffPSR();
      double diffPSR_ratio = getPwlFactor(diffPSR, diff_area, 0.0);
      if (diffPSR_ratio != 0 && diff_psr > diffPSR_ratio)
        wire_PAR_violation = 1;
    }
  }

  return wire_PAR_violation;
}

std::vector<ViolationInfo> AntennaChecker::getNetAntennaViolations(
    dbNet* net,
    std::string antenna_cell_name,
    std::string cell_pin)
{
  double max_diff_area = 0;
  if (!antenna_cell_name.empty()) {
    odb::dbMaster* antenna_cell = db_->findMaster(antenna_cell_name.c_str());
    dbMTerm* mterm = antenna_cell->findMTerm(cell_pin.c_str());
    std::vector<std::pair<double, dbTechLayer*>> diff_area;
    mterm->getDiffArea(diff_area);

    std::vector<std::pair<double, dbTechLayer*>>::iterator diff_area_iter;
    for (diff_area_iter = diff_area.begin(); diff_area_iter != diff_area.end();
         diff_area_iter++)
      max_diff_area = std::max(max_diff_area, (*diff_area_iter).first);
  }

  std::vector<ViolationInfo> antenna_violations;
  if (net->isSpecial())
    return antenna_violations;
  dbWire* wire = net->getWire();
  dbWireGraph graph;
  if (wire) {
    graph.decode(wire);

    auto wireroots_info = getWireroots(graph);

    std::vector<PARinfo> PARtable;
    buildWireParTable(PARtable, wireroots_info);

    std::vector<PARinfo>::iterator par_itr;

    for (par_itr = PARtable.begin(); par_itr != PARtable.end(); ++par_itr) {
      dbTechLayer* layer = (*par_itr).WirerootNode->layer();
      bool wire_PAR_violation = checkViolation(*par_itr, layer);

      if (wire_PAR_violation) {
        std::vector<dbITerm*> gates;
        findWirerootIterms(
            (*par_itr).WirerootNode, layer->getRoutingLevel(), gates);
        int required_cell_nums = 0;
        if (!antenna_cell_name.empty()) {
          while (wire_PAR_violation && required_cell_nums < 10) {
            (*par_itr).iterm_areas[1]
                += max_diff_area * ((*par_itr).iterms.size());
            required_cell_nums++;
            calculateParInfo(*par_itr);
            wire_PAR_violation = checkViolation(*par_itr, layer);
          }

          // std::cout << "Insts: ";
          // for (dbITerm* iterm: gates)
          //{
          //    dbInst* inst = iterm->getInst();
          //    dbMTerm* mterm = iterm->getMTerm();
          //    std::cout << inst->getName() << "-" << mterm->getName()<< " ";
          //}
          // std::cout << "\n  Requires " << required_cell_nums << " diodes to
          // remove antenna violation"<<std::endl;
        }
        ViolationInfo antenna_violation
            = {layer->getRoutingLevel(), gates, required_cell_nums};
        antenna_violations.push_back(antenna_violation);
      }
    }
  }
  return antenna_violations;
}

std::vector<std::pair<double, std::vector<dbITerm*>>>
AntennaChecker::getViolatedWireLength(dbNet* net, int routing_level)
{
  std::vector<std::pair<double, std::vector<dbITerm*>>> violated_wires;
  if (net->isSpecial() || net->getWire() == nullptr)
    return violated_wires;
  dbWire* wire = net->getWire();

  dbWireGraph graph;
  graph.decode(wire);

  auto wireroots_info = getWireroots(graph);

  std::set<dbWireGraph::Node*> level_nodes;
  for (auto root_itr = wireroots_info.begin(); root_itr != wireroots_info.end();
       ++root_itr) {
    dbWireGraph::Node* wireroot = *root_itr;
    odb::dbTechLayer* tech_layer = wireroot->layer();
    if (level_nodes.find(wireroot) == level_nodes.end()
        && tech_layer->getRoutingLevel() == routing_level) {
      std::set<dbWireGraph::Node*> nv;
      auto areas = calculateWireArea(
          wireroot, tech_layer->getRoutingLevel(), nv, level_nodes);
      double wire_area = areas.first;
      double iterm_areas[2] = {0.0, 0.0};

      std::set<dbITerm*> iv;
      nv.clear();
      findWireBelowIterms(
          wireroot, iterm_areas, tech_layer->getRoutingLevel(), iv, nv);
      if (iterm_areas[0] == 0)
        continue;

      double wire_width = defDist(tech_layer->getWidth());

      ANTENNAmodel am = layer_info[tech_layer];
      double metal_factor = am.metal_factor;
      double diff_metal_factor = am.diff_metal_factor;

      double minus_diff_factor = am.minus_diff_factor;
      double plus_diff_factor = am.plus_diff_factor;
      double diff_metal_reduce_factor = am.diff_metal_reduce_factor;

      if (wireroot->layer()->hasDefaultAntennaRule()) {
        dbTechLayerAntennaRule* antenna_rule
            = tech_layer->getDefaultAntennaRule();
        diff_metal_reduce_factor = getPwlFactor(
            antenna_rule->getAreaDiffReduce(), iterm_areas[1], 1.0);

        double par = 0;
        double diff_par = 0;

        if (iterm_areas[1] != 0) {
          par = (diff_metal_factor * wire_area) / iterm_areas[0];
          diff_par = (diff_metal_factor * wire_area * diff_metal_reduce_factor
                      - minus_diff_factor * iterm_areas[1])
                     / (iterm_areas[0] + plus_diff_factor * iterm_areas[1]);
        } else {
          par = (metal_factor * wire_area) / iterm_areas[0];
          diff_par = (metal_factor * wire_area * diff_metal_reduce_factor)
                     / iterm_areas[0];
        }

        double cut_length = 0;
        double PAR_ratio = antenna_rule->getPAR();
        if (PAR_ratio != 0) {
          if (par > PAR_ratio) {
            if (iterm_areas[1] != 0)
              cut_length = ((par - PAR_ratio) * iterm_areas[0]
                            - diff_metal_factor * wire_area)
                           / wire_width;
            else
              cut_length = ((par - PAR_ratio) * iterm_areas[0]
                            - metal_factor * wire_area)
                           / wire_width;
          }

        } else {
          dbTechLayerAntennaRule::pwl_pair diffPAR = antenna_rule->getDiffPAR();
          double diffPAR_ratio = getPwlFactor(diffPAR, iterm_areas[1], 0.0);
          if (iterm_areas[1] != 0)
            cut_length
                = ((diff_par - diffPAR_ratio)
                       * (iterm_areas[0] + plus_diff_factor * iterm_areas[1])
                   - (diff_metal_factor * wire_area * diff_metal_reduce_factor
                      - minus_diff_factor * iterm_areas[1]))
                  / wire_width;
          else
            cut_length
                = ((diff_par - diffPAR_ratio)
                       * (iterm_areas[0] + plus_diff_factor * iterm_areas[1])
                   - (metal_factor * wire_area * diff_metal_reduce_factor
                      - minus_diff_factor * iterm_areas[1]))
                  / wire_width;
        }

        if (cut_length != 0) {
          std::vector<dbITerm*> gates;
          findWirerootIterms(wireroot, routing_level, gates);
          std::pair<double, std::vector<dbITerm*>> violated_wire
              = std::make_pair(cut_length, gates);
          violated_wires.push_back(violated_wire);
        }
      }
    }
  }
  return violated_wires;
}

void AntennaChecker::findMaxWireLength()
{
  dbSet<dbNet> nets = db_->getChip()->getBlock()->getNets();
  std::string max_wire_name = "";
  double max_wire_length = 0.0;

  dbSet<dbNet>::iterator net_itr;
  for (net_itr = nets.begin(); net_itr != nets.end(); ++net_itr) {
    dbNet* net = *net_itr;
    dbWire* wire = net->getWire();
    if (!net->isSpecial() && wire != nullptr) {
      dbWireGraph graph;
      graph.decode(wire);

      double wire_length = 0;
      dbWireGraph::edge_iterator edge_itr;
      for (edge_itr = graph.begin_edges(); edge_itr != graph.end_edges();
           ++edge_itr) {
        dbWireGraph::Edge* edge = *edge_itr;
        int x1, y1, x2, y2;
        edge->source()->xy(x1, y1);
        edge->target()->xy(x2, y2);
        if (edge->type() == dbWireGraph::Edge::Type::SEGMENT
            || edge->type() == dbWireGraph::Edge::Type::SHORT)
          wire_length += defDist((abs(x2 - x1) + abs(y2 - y1)));
      }

      if (wire_length > max_wire_length) {
        max_wire_length = wire_length;
        max_wire_name = std::string(net->getConstName());
      }
    }
  }

  std::cout << "wire name: " << max_wire_name << "\n"
            << "wire length: " << max_wire_length << std::endl;
}

}  // namespace ant
