/*
    EQ2Emulator:  Everquest II Server Emulator
    Copyright (C) 2005 - 2026  EQ2EMulator Development Team

    This file is part of EQ2Emulator.

    EQ2Emulator is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "HTTPSClientPool.h"
#include "../World.h"
#include "../WorldDatabase.h"
#include "../client.h"
#include "../Player.h"
#include "../zoneserver.h"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

extern WorldDatabase database;
extern ZoneList zone_list;

namespace {
using boost::property_tree::ptree;

static const char* kJsonType = "application/json; charset=utf-8";
static const char* kHtmlType = "text/html; charset=utf-8";

static std::string RowString(MYSQL_ROW row, unsigned int index) {
	return row && row[index] ? std::string(row[index]) : std::string("");
}

static int32 RowInt(MYSQL_ROW row, unsigned int index) {
	return row && row[index] ? static_cast<int32>(strtol(row[index], nullptr, 10)) : 0;
}

static float RowFloat(MYSQL_ROW row, unsigned int index) {
	return row && row[index] ? static_cast<float>(atof(row[index])) : 0.0f;
}

static std::string EscapeSql(const std::string& value) {
	return database.getSafeEscapeString(value.c_str());
}

static std::string EscapeLuaString(const std::string& value) {
	std::string out;
	out.reserve(value.size());
	for (char c : value) {
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': break;
		case '\t': out += "\\t"; break;
		default: out += c; break;
		}
	}
	return out;
}

static std::string Slugify(const std::string& value) {
	std::string slug;
	for (char c : value) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
			slug += static_cast<char>(tolower(c));
		}
		else if (c == ' ' || c == '_' || c == '-') {
			if (!slug.empty() && slug.back() != '_')
				slug += '_';
		}
	}
	boost::algorithm::trim_if(slug, boost::is_any_of("_"));
	if (slug.empty())
		slug = "content";
	return slug.substr(0, 48);
}

static bool ReadJsonBody(const http::request<http::string_body>& req, ptree& body, ptree& response) {
	if (req.body().empty())
		return true;

	try {
		std::istringstream stream(req.body());
		boost::property_tree::read_json(stream, body);
		return true;
	}
	catch (const std::exception& e) {
		response.put("success", 0);
		response.put("error", std::string("Invalid JSON body: ") + e.what());
		return false;
	}
}

static std::string GetString(const ptree& pt, const std::string& key, const std::string& fallback = "") {
	auto value = pt.get_optional<std::string>(key);
	return value ? value.get() : fallback;
}

static int32 GetInt(const ptree& pt, const std::string& key, int32 fallback = 0) {
	auto value = pt.get_optional<int32>(key);
	return value ? value.get() : fallback;
}

static float GetFloat(const ptree& pt, const std::string& key, float fallback = 0.0f) {
	auto value = pt.get_optional<float>(key);
	return value ? value.get() : fallback;
}

static bool GetBool(const ptree& pt, const std::string& key, bool fallback = false) {
	auto value = pt.get_optional<std::string>(key);
	if (!value)
		return fallback;

	std::string lowered = boost::algorithm::to_lower_copy(value.get());
	return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

static std::vector<int32> ParseIdList(const std::string& text) {
	std::vector<int32> ids;
	std::vector<std::string> parts;
	boost::split(parts, text, boost::is_any_of(", \n\t\r;"));
	for (auto& part : parts) {
		boost::algorithm::trim(part);
		if (!part.empty() && std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; }))
			ids.push_back(static_cast<int32>(strtol(part.c_str(), nullptr, 10)));
	}
	return ids;
}

static void WriteJson(http::response<http::string_body>& res, const ptree& pt) {
	res.set(http::field::content_type, kJsonType);
	std::ostringstream out;
	boost::property_tree::write_json(out, pt);
	res.body() = out.str();
	res.prepare_payload();
}

static void AddIssue(ptree& issues, const std::string& severity, const std::string& area, const std::string& subject, const std::string& detail, const std::string& fix) {
	ptree issue;
	issue.put("severity", severity);
	issue.put("area", area);
	issue.put("subject", subject);
	issue.put("detail", detail);
	issue.put("fix", fix);
	issues.push_back(std::make_pair("", issue));
}

static void AddPlan(ptree& plan, const std::string& text) {
	ptree item;
	item.put("", text);
	plan.push_back(std::make_pair("", item));
}

static bool ScriptPathExists(const std::string& script) {
	if (script.empty())
		return false;

	boost::filesystem::path raw(script);
	if (boost::filesystem::exists(raw))
		return true;

	boost::filesystem::path server_path = boost::filesystem::path("server") / raw;
	if (boost::filesystem::exists(server_path))
		return true;

	return false;
}

static std::string WhitelistItemType(const std::string& raw) {
	static const std::set<std::string> allowed = {
		"Normal", "Weapon", "Ranged", "Armor", "Shield", "Bag", "Scroll", "Recipe",
		"Food", "Bauble", "House", "Thrown"
	};
	return allowed.count(raw) ? raw : "Normal";
}

static std::string WhitelistHarvestSkill(const std::string& raw) {
	static const std::set<std::string> allowed = {
		"Unused", "Mining", "Gathering", "Fishing", "Trapping", "Foresting", "Collecting"
	};
	return allowed.count(raw) ? raw : "Gathering";
}

static void PopulateZones(ptree& root) {
	ptree zones;
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT id, name, description, file, min_recommended, max_recommended "
		"FROM zones ORDER BY name LIMIT 2000");

	while (result && (row = mysql_fetch_row(result))) {
		ptree zone;
		zone.put("id", RowInt(row, 0));
		zone.put("name", RowString(row, 1));
		zone.put("description", RowString(row, 2));
		zone.put("file", RowString(row, 3));
		zone.put("min_recommended", RowInt(row, 4));
		zone.put("max_recommended", RowInt(row, 5));
		zones.push_back(std::make_pair("", zone));
	}
	root.add_child("zones", zones);
}

static bool PopulateZoneHeader(int32 zone_id, ptree& root, std::string& zone_name) {
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT id, name, description, file, min_recommended, max_recommended, safe_x, safe_y, safe_z, safe_heading, lua_script "
		"FROM zones WHERE id=%u LIMIT 1",
		zone_id);

	if (!result || !(row = mysql_fetch_row(result)))
		return false;

	zone_name = RowString(row, 1);
	ptree zone;
	zone.put("id", RowInt(row, 0));
	zone.put("name", zone_name);
	zone.put("description", RowString(row, 2));
	zone.put("file", RowString(row, 3));
	zone.put("min_recommended", RowInt(row, 4));
	zone.put("max_recommended", RowInt(row, 5));
	zone.put("safe_x", RowFloat(row, 6));
	zone.put("safe_y", RowFloat(row, 7));
	zone.put("safe_z", RowFloat(row, 8));
	zone.put("safe_heading", RowFloat(row, 9));
	zone.put("lua_script", RowString(row, 10));
	root.add_child("zone", zone);
	return true;
}

static int32 ScalarCount(const char* sql, int32 zone_id) {
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT, sql, zone_id);
	if (result && (row = mysql_fetch_row(result)))
		return RowInt(row, 0);
	return 0;
}

static void PopulateZoneSummary(int32 zone_id, const std::string& zone_name, ptree& root) {
	ptree summary;
	summary.put("placements", ScalarCount("SELECT COUNT(*) FROM spawn_location_placement WHERE zone_id=%u", zone_id));
	summary.put("unique_spawns", ScalarCount(
		"SELECT COUNT(DISTINCT e.spawn_id) "
		"FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"WHERE p.zone_id=%u", zone_id));
	summary.put("npcs", ScalarCount(
		"SELECT COUNT(DISTINCT s.id) "
		"FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id INNER JOIN spawn_npcs n ON n.spawn_id=s.id "
		"WHERE p.zone_id=%u", zone_id));
	summary.put("hostile_npcs", ScalarCount(
		"SELECT COUNT(DISTINCT s.id) "
		"FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id INNER JOIN spawn_npcs n ON n.spawn_id=s.id "
		"WHERE p.zone_id=%u AND s.attackable > 0", zone_id));
	summary.put("groundspawns", ScalarCount(
		"SELECT COUNT(DISTINCT s.id) "
		"FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id INNER JOIN spawn_ground g ON g.spawn_id=s.id "
		"WHERE p.zone_id=%u", zone_id));
	summary.put("pois", ScalarCount("SELECT COUNT(*) FROM locations WHERE zone_id=%u", zone_id));
	summary.put("merchants", ScalarCount(
		"SELECT COUNT(DISTINCT s.id) "
		"FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id WHERE p.zone_id=%u AND s.merchant_id > 0", zone_id));
	summary.put("transporters", ScalarCount(
		"SELECT COUNT(DISTINCT s.id) "
		"FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id WHERE p.zone_id=%u AND s.transport_id > 0", zone_id));

	std::string escaped_zone = EscapeSql(zone_name);
	Query quest_query;
	MYSQL_ROW quest_row;
	MYSQL_RES* quest_result = quest_query.RunQuery2(Q_SELECT,
		"SELECT COUNT(DISTINCT q.quest_id) "
		"FROM quests q "
		"LEFT JOIN spawn_location_entry e ON e.spawn_id=q.spawn_id "
		"LEFT JOIN spawn_location_placement p ON p.spawn_location_id=e.spawn_location_id "
		"WHERE q.zone='%s' OR p.zone_id=%u",
		escaped_zone.c_str(), zone_id);
	if (quest_result && (quest_row = mysql_fetch_row(quest_result)))
		summary.put("quests", RowInt(quest_row, 0));
	else
		summary.put("quests", 0);

	root.add_child("summary", summary);
}

static void PopulateNpcList(int32 zone_id, ptree& root) {
	ptree npcs;
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT s.id, s.name, s.model_type, s.size, s.attackable, s.targetable, s.faction_id, "
		"n.min_level, n.max_level, n.heroic_flag, n.aggro_radius, "
		"COUNT(DISTINCT p.id), COUNT(DISTINCT sl.loottable_id), MIN(p.x), MIN(p.y), MIN(p.z) "
		"FROM spawn_location_placement p "
		"INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id "
		"INNER JOIN spawn_npcs n ON n.spawn_id=s.id "
		"LEFT JOIN spawn_loot sl ON sl.spawn_id=s.id "
		"WHERE p.zone_id=%u "
		"GROUP BY s.id, s.name, s.model_type, s.size, s.attackable, s.targetable, s.faction_id, n.min_level, n.max_level, n.heroic_flag, n.aggro_radius "
		"ORDER BY n.min_level, s.name LIMIT 300",
		zone_id);

	while (result && (row = mysql_fetch_row(result))) {
		ptree npc;
		npc.put("spawn_id", RowInt(row, 0));
		npc.put("name", RowString(row, 1));
		npc.put("model_type", RowInt(row, 2));
		npc.put("size", RowInt(row, 3));
		npc.put("attackable", RowInt(row, 4));
		npc.put("targetable", RowInt(row, 5));
		npc.put("faction_id", RowInt(row, 6));
		npc.put("min_level", RowInt(row, 7));
		npc.put("max_level", RowInt(row, 8));
		npc.put("heroic_flag", RowInt(row, 9));
		npc.put("aggro_radius", RowFloat(row, 10));
		npc.put("placements", RowInt(row, 11));
		npc.put("loot_tables", RowInt(row, 12));
		npc.put("x", RowFloat(row, 13));
		npc.put("y", RowFloat(row, 14));
		npc.put("z", RowFloat(row, 15));
		npcs.push_back(std::make_pair("", npc));
	}
	root.add_child("npcs", npcs);
}

static void PopulateGroundspawnList(int32 zone_id, ptree& root) {
	ptree nodes;
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT s.id, s.name, g.groundspawn_id, g.collection_skill, gs.min_skill_level, gs.min_adventure_level, "
		"COUNT(DISTINCT gi.item_id), COUNT(DISTINCT p.id), MIN(p.x), MIN(p.y), MIN(p.z) "
		"FROM spawn_location_placement p "
		"INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id "
		"INNER JOIN spawn_ground g ON g.spawn_id=s.id "
		"LEFT JOIN groundspawns gs ON gs.groundspawn_id=g.groundspawn_id "
		"LEFT JOIN groundspawn_items gi ON gi.groundspawn_id=g.groundspawn_id "
		"WHERE p.zone_id=%u "
		"GROUP BY s.id, s.name, g.groundspawn_id, g.collection_skill, gs.min_skill_level, gs.min_adventure_level "
		"ORDER BY g.collection_skill, s.name LIMIT 250",
		zone_id);

	while (result && (row = mysql_fetch_row(result))) {
		ptree node;
		node.put("spawn_id", RowInt(row, 0));
		node.put("name", RowString(row, 1));
		node.put("groundspawn_id", RowInt(row, 2));
		node.put("skill", RowString(row, 3));
		node.put("min_skill_level", RowInt(row, 4));
		node.put("min_adventure_level", RowInt(row, 5));
		node.put("item_count", RowInt(row, 6));
		node.put("placements", RowInt(row, 7));
		node.put("x", RowFloat(row, 8));
		node.put("y", RowFloat(row, 9));
		node.put("z", RowFloat(row, 10));
		nodes.push_back(std::make_pair("", node));
	}
	root.add_child("groundspawns", nodes);
}

static void PopulateQuestList(int32 zone_id, const std::string& zone_name, ptree& root) {
	ptree quests;
	std::string escaped_zone = EscapeSql(zone_name);
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT q.quest_id, q.name, q.type, q.zone, q.level, q.enc_level, q.spawn_id, COALESCE(s.name, ''), q.lua_script, "
		"COUNT(DISTINCT qd.id), COUNT(DISTINCT p.id) "
		"FROM quests q "
		"LEFT JOIN spawn s ON s.id=q.spawn_id "
		"LEFT JOIN quest_details qd ON qd.quest_id=q.quest_id "
		"LEFT JOIN spawn_location_entry e ON e.spawn_id=q.spawn_id "
		"LEFT JOIN spawn_location_placement p ON p.spawn_location_id=e.spawn_location_id "
		"WHERE q.zone='%s' OR p.zone_id=%u "
		"GROUP BY q.quest_id, q.name, q.type, q.zone, q.level, q.enc_level, q.spawn_id, s.name, q.lua_script "
		"ORDER BY q.level, q.name LIMIT 300",
		escaped_zone.c_str(), zone_id);

	while (result && (row = mysql_fetch_row(result))) {
		ptree quest;
		quest.put("quest_id", RowInt(row, 0));
		quest.put("name", RowString(row, 1));
		quest.put("type", RowString(row, 2));
		quest.put("zone", RowString(row, 3));
		quest.put("level", RowInt(row, 4));
		quest.put("enc_level", RowInt(row, 5));
		quest.put("spawn_id", RowInt(row, 6));
		quest.put("starter", RowString(row, 7));
		quest.put("lua_script", RowString(row, 8));
		quest.put("detail_count", RowInt(row, 9));
		quest.put("starter_placements", RowInt(row, 10));
		quests.push_back(std::make_pair("", quest));
	}
	root.add_child("quests", quests);
}

static void PopulatePoiList(int32 zone_id, ptree& root) {
	ptree pois;
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT l.id, l.name, l.grid_id, l.discovery, l.include_y, MIN(d.x), MIN(d.y), MIN(d.z), COUNT(d.id) "
		"FROM locations l LEFT JOIN location_details d ON d.location_id=l.id "
		"WHERE l.zone_id=%u "
		"GROUP BY l.id, l.name, l.grid_id, l.discovery, l.include_y "
		"ORDER BY l.name LIMIT 250",
		zone_id);

	while (result && (row = mysql_fetch_row(result))) {
		ptree poi;
		poi.put("id", RowInt(row, 0));
		poi.put("name", RowString(row, 1));
		poi.put("grid_id", RowInt(row, 2));
		poi.put("discovery", RowInt(row, 3));
		poi.put("include_y", RowInt(row, 4));
		poi.put("x", RowFloat(row, 5));
		poi.put("y", RowFloat(row, 6));
		poi.put("z", RowFloat(row, 7));
		poi.put("points", RowInt(row, 8));
		pois.push_back(std::make_pair("", poi));
	}
	root.add_child("pois", pois);
}

static void PopulateMapPoints(int32 zone_id, ptree& root) {
	ptree points;
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT p.id, p.x, p.y, p.z, s.id, s.name, "
		"CASE WHEN n.spawn_id IS NOT NULL THEN 'npc' WHEN g.spawn_id IS NOT NULL THEN 'harvest' WHEN w.spawn_id IS NOT NULL THEN 'widget' WHEN si.spawn_id IS NOT NULL THEN 'sign' ELSE 'object' END, "
		"s.attackable "
		"FROM spawn_location_placement p "
		"INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id "
		"LEFT JOIN spawn_npcs n ON n.spawn_id=s.id "
		"LEFT JOIN spawn_ground g ON g.spawn_id=s.id "
		"LEFT JOIN spawn_widgets w ON w.spawn_id=s.id "
		"LEFT JOIN spawn_signs si ON si.spawn_id=s.id "
		"WHERE p.zone_id=%u "
		"ORDER BY s.name, p.id LIMIT 1500",
		zone_id);

	while (result && (row = mysql_fetch_row(result))) {
		ptree point;
		point.put("placement_id", RowInt(row, 0));
		point.put("x", RowFloat(row, 1));
		point.put("y", RowFloat(row, 2));
		point.put("z", RowFloat(row, 3));
		point.put("spawn_id", RowInt(row, 4));
		point.put("name", RowString(row, 5));
		point.put("type", RowString(row, 6));
		point.put("attackable", RowInt(row, 7));
		points.push_back(std::make_pair("", point));
	}
	root.add_child("map_points", points);
}

static void PopulateAudit(int32 zone_id, const std::string& zone_name, ptree& root) {
	ptree issues;

	Query placements_without_entries;
	MYSQL_ROW row;
	MYSQL_RES* result = placements_without_entries.RunQuery2(Q_SELECT,
		"SELECT p.id, p.spawn_location_id "
		"FROM spawn_location_placement p "
		"LEFT JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"WHERE p.zone_id=%u AND e.id IS NULL LIMIT 50",
		zone_id);
	while (result && (row = mysql_fetch_row(result))) {
		AddIssue(issues, "error", "Spawns", "Placement has no spawn entry",
			"Placement " + RowString(row, 0) + " points at spawn_location_id " + RowString(row, 1) + " but no spawn_location_entry rows use it.",
			"Add a spawn_location_entry row or remove the unused placement.");
	}

	Query hostile_without_loot;
	result = hostile_without_loot.RunQuery2(Q_SELECT,
		"SELECT DISTINCT s.id, s.name "
		"FROM spawn_location_placement p "
		"INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id "
		"INNER JOIN spawn_npcs n ON n.spawn_id=s.id "
		"LEFT JOIN spawn_loot sl ON sl.spawn_id=s.id "
		"WHERE p.zone_id=%u AND s.attackable > 0 AND sl.id IS NULL "
		"ORDER BY s.name LIMIT 75",
		zone_id);
	while (result && (row = mysql_fetch_row(result))) {
		AddIssue(issues, "warning", "Loot", "Attackable NPC has no loot table",
			RowString(row, 1) + " (" + RowString(row, 0) + ") is attackable but has no spawn_loot row.",
			"Attach a loot table or intentionally mark it as no-loot in developer notes.");
	}

	Query harvest_without_items;
	result = harvest_without_items.RunQuery2(Q_SELECT,
		"SELECT DISTINCT s.id, s.name, g.groundspawn_id "
		"FROM spawn_location_placement p "
		"INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id "
		"INNER JOIN spawn_ground g ON g.spawn_id=s.id "
		"LEFT JOIN groundspawn_items gi ON gi.groundspawn_id=g.groundspawn_id "
		"WHERE p.zone_id=%u AND gi.id IS NULL "
		"ORDER BY s.name LIMIT 75",
		zone_id);
	while (result && (row = mysql_fetch_row(result))) {
		AddIssue(issues, "error", "Harvesting", "Harvest node has no item table",
			RowString(row, 1) + " uses groundspawn_id " + RowString(row, 2) + " without groundspawn_items.",
			"Add at least one groundspawn_items row for this groundspawn_id.");
	}

	std::string escaped_zone = EscapeSql(zone_name);
	Query quest_missing_starter;
	result = quest_missing_starter.RunQuery2(Q_SELECT,
		"SELECT q.quest_id, q.name, q.spawn_id "
		"FROM quests q LEFT JOIN spawn s ON s.id=q.spawn_id "
		"WHERE (q.zone='%s' OR q.spawn_id IN ("
		"SELECT e.spawn_id FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id WHERE p.zone_id=%u"
		")) AND q.spawn_id > 0 AND s.id IS NULL LIMIT 75",
		escaped_zone.c_str(), zone_id);
	while (result && (row = mysql_fetch_row(result))) {
		AddIssue(issues, "error", "Quests", "Quest starter spawn is missing",
			RowString(row, 1) + " (" + RowString(row, 0) + ") references spawn_id " + RowString(row, 2) + ".",
			"Create the starter NPC or update quests.spawn_id.");
	}

	Query duplicate_names;
	result = duplicate_names.RunQuery2(Q_SELECT,
		"SELECT s.name, COUNT(DISTINCT s.id) "
		"FROM spawn_location_placement p "
		"INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id "
		"INNER JOIN spawn s ON s.id=e.spawn_id "
		"WHERE p.zone_id=%u "
		"GROUP BY s.name HAVING COUNT(DISTINCT s.id) > 1 "
		"ORDER BY COUNT(DISTINCT s.id) DESC, s.name LIMIT 75",
		zone_id);
	while (result && (row = mysql_fetch_row(result))) {
		AddIssue(issues, "info", "Spawns", "Duplicate spawn name",
			RowString(row, 0) + " appears on " + RowString(row, 1) + " distinct spawn rows in this zone.",
			"Use subtitles, suffixes, or consolidate duplicate rows when the duplicates are accidental.");
	}

	Query broken_rewards;
	result = broken_rewards.RunQuery2(Q_SELECT,
		"SELECT q.quest_id, q.name, qd.value "
		"FROM quests q "
		"INNER JOIN quest_details qd ON qd.quest_id=q.quest_id "
		"LEFT JOIN items i ON i.id=qd.value "
		"WHERE (q.zone='%s' OR q.spawn_id IN ("
		"SELECT e.spawn_id FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id WHERE p.zone_id=%u"
		")) AND qd.subtype IN ('Item','Selectable') AND qd.value > 0 AND i.id IS NULL LIMIT 75",
		escaped_zone.c_str(), zone_id);
	while (result && (row = mysql_fetch_row(result))) {
		AddIssue(issues, "error", "Quest Rewards", "Quest item reference is missing",
			RowString(row, 1) + " (" + RowString(row, 0) + ") references missing item " + RowString(row, 2) + ".",
			"Create the item or update the quest reward/prerequisite detail.");
	}

	Query quest_scripts;
	result = quest_scripts.RunQuery2(Q_SELECT,
		"SELECT q.quest_id, q.name, q.lua_script "
		"FROM quests q WHERE (q.zone='%s' OR q.spawn_id IN ("
		"SELECT e.spawn_id FROM spawn_location_placement p INNER JOIN spawn_location_entry e ON e.spawn_location_id=p.spawn_location_id WHERE p.zone_id=%u"
		")) AND COALESCE(q.lua_script, '') <> '' LIMIT 200",
		escaped_zone.c_str(), zone_id);
	while (result && (row = mysql_fetch_row(result))) {
		std::string script = RowString(row, 2);
		if (!ScriptPathExists(script)) {
			AddIssue(issues, "warning", "Quest Scripts", "Quest script path was not found",
				RowString(row, 1) + " points to " + script + ".",
				"Verify the script exists relative to the server working directory.");
		}
	}

	root.add_child("audit", issues);
}

static void PopulateQuestValidation(int32 zone_id, const std::string& zone_name, ptree& root) {
	ptree validations;
	std::string escaped_zone = EscapeSql(zone_name);

	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT,
		"SELECT q.quest_id, q.name, "
		"SUM(CASE WHEN q.spawn_id > 0 AND s.id IS NULL THEN 1 ELSE 0 END), "
		"SUM(CASE WHEN q.spawn_id > 0 AND p.id IS NULL THEN 1 ELSE 0 END), "
		"SUM(CASE WHEN qd.subtype IN ('Item','Selectable') AND qd.value > 0 AND i.id IS NULL THEN 1 ELSE 0 END), "
		"SUM(CASE WHEN qd.subtype='Quest' AND qd.value > 0 AND pq.quest_id IS NULL THEN 1 ELSE 0 END), "
		"q.lua_script "
		"FROM quests q "
		"LEFT JOIN spawn s ON s.id=q.spawn_id "
		"LEFT JOIN spawn_location_entry e ON e.spawn_id=q.spawn_id "
		"LEFT JOIN spawn_location_placement p ON p.spawn_location_id=e.spawn_location_id AND p.zone_id=%u "
		"LEFT JOIN quest_details qd ON qd.quest_id=q.quest_id "
		"LEFT JOIN items i ON i.id=qd.value AND qd.subtype IN ('Item','Selectable') "
		"LEFT JOIN quests pq ON pq.quest_id=qd.value AND qd.subtype='Quest' "
		"WHERE q.zone='%s' OR p.zone_id=%u "
		"GROUP BY q.quest_id, q.name, q.lua_script "
		"ORDER BY q.name LIMIT 300",
		zone_id, escaped_zone.c_str(), zone_id);

	while (result && (row = mysql_fetch_row(result))) {
		int32 starter_missing = RowInt(row, 2);
		int32 starter_outside_zone = RowInt(row, 3);
		int32 missing_items = RowInt(row, 4);
		int32 missing_prereq_quests = RowInt(row, 5);
		std::string script = RowString(row, 6);
		bool missing_script = !script.empty() && !ScriptPathExists(script);

		ptree validation;
		validation.put("quest_id", RowInt(row, 0));
		validation.put("name", RowString(row, 1));
		validation.put("starter_missing", starter_missing);
		validation.put("starter_outside_zone", starter_outside_zone);
		validation.put("missing_items", missing_items);
		validation.put("missing_prereq_quests", missing_prereq_quests);
		validation.put("script_missing", missing_script ? 1 : 0);
		validation.put("lua_script", script);
		validation.put("status", (starter_missing || missing_items || missing_prereq_quests || missing_script) ? "needs attention" : "ok");
		validations.push_back(std::make_pair("", validation));
	}

	root.add_child("quest_validation", validations);
}

static std::string BuildDialogLua(const ptree& body, const std::string& script_name) {
	std::string hail = GetString(body, "hail_text", "Hello there.");
	int32 quest_id = GetInt(body, "quest_id", 0);
	std::string quest_option = GetString(body, "quest_option", "I can help.");
	std::string close_option = GetString(body, "close_option", "Maybe later.");

	std::ostringstream lua;
	lua << "-- Generated by the EQ2Emu content workbench: " << script_name << "\n\n";
	lua << "function spawn(NPC)\n";
	lua << "end\n\n";
	lua << "function hailed(NPC, Spawn)\n";
	lua << "    FaceTarget(NPC, Spawn)\n";
	lua << "    local conversation = CreateConversation()\n";
	if (quest_id > 0)
		lua << "    AddConversationOption(conversation, \"" << EscapeLuaString(quest_option) << "\", \"OfferGeneratedQuest\")\n";
	lua << "    AddConversationOption(conversation, \"" << EscapeLuaString(close_option) << "\")\n";
	lua << "    StartConversation(conversation, NPC, Spawn, \"" << EscapeLuaString(hail) << "\")\n";
	lua << "end\n";
	if (quest_id > 0) {
		lua << "\nfunction OfferGeneratedQuest(NPC, Spawn)\n";
		lua << "    OfferQuest(NPC, Spawn, " << quest_id << ")\n";
		lua << "end\n";
	}
	return lua.str();
}

static void AddRouteInfo(ptree& routes, const std::string& route, const std::string& method, const std::string& group, const std::string& description, const std::string& payload = "") {
	ptree item;
	item.put("route", route);
	item.put("method", method);
	item.put("group", group);
	item.put("description", description);
	item.put("payload", payload);
	routes.push_back(std::make_pair("", item));
}

static void PopulateRouteMetadata(ptree& root) {
	ptree routes;
	AddRouteInfo(routes, "/", "GET", "UI", "World web dashboard.");
	AddRouteInfo(routes, "/ui", "GET", "UI", "World web dashboard.");
	AddRouteInfo(routes, "/content", "GET", "UI", "Content workbench.");
	AddRouteInfo(routes, "/routes", "GET", "UI", "Route metadata used by the dashboard.");
	AddRouteInfo(routes, "/version", "GET", "Status", "Build and module metadata.");
	AddRouteInfo(routes, "/status", "GET", "Status", "World, login, peer, and uptime status.");
	AddRouteInfo(routes, "/clients", "GET", "Status", "Connected client and character details.");
	AddRouteInfo(routes, "/zones", "GET", "Status", "Active zone server details.");
	AddRouteInfo(routes, "/content/api/bootstrap", "POST", "Content", "Content workbench bootstrap data.", "{}");
	AddRouteInfo(routes, "/content/api/zone", "POST", "Content", "Full content manifest for a zone.", "{\"zone_id\":1}");
	AddRouteInfo(routes, "/content/api/search", "POST", "Content", "Model and NPC model search.", "{\"search\":\"gnoll\"}");
	AddRouteInfo(routes, "/content/api/apply", "POST", "Content", "Dry-run or apply a content builder action.", "{\"action\":\"npc\",\"dry_run\":true}");
	AddRouteInfo(routes, "/reloadrules", "POST", "Admin", "Reload world rules.", "{}");
	AddRouteInfo(routes, "/reloadcommand", "POST", "Admin", "Run a reload command by command id.", "{\"reload_command\":0,\"sub_command\":0}");
	AddRouteInfo(routes, "/setadminstatus", "POST", "Admin", "Set an online or stored character admin status.", "{\"character_name\":\"Name\",\"new_status\":200}");
	AddRouteInfo(routes, "/startzone", "POST", "Admin", "Start or locate a zone.", "{\"zone_name\":\"antonica\"}");
	AddRouteInfo(routes, "/addcharauth", "POST", "Admin", "Add temporary character zone authorization.", "{\"account_id\":1,\"character_id\":1,\"character_name\":\"Name\",\"zone_name\":\"zone\",\"client_ip\":\"127.0.0.1\"}");
	AddRouteInfo(routes, "/sendglobalmessage", "POST", "Admin", "Send a global message.", "{\"from_name\":\"Server\",\"to_name\":\"\",\"message\":\"Message\",\"channel\":92}");
	AddRouteInfo(routes, "/activequery", "POST", "Admin", "Check active DB query state.", "{\"character_id\":1}");
	AddRouteInfo(routes, "/peerstatus", "GET", "Peering", "Peer status snapshot.");
	AddRouteInfo(routes, "/addpeer", "POST", "Peering", "Register or update a web peer.", "{\"client_address\":\"127.0.0.1\",\"client_port\":9001,\"web_address\":\"127.0.0.1\",\"web_port\":8080}");
	AddRouteInfo(routes, "/newgroup", "POST", "Groups", "Create a group.", "{\"leader\":\"Leader\",\"member\":\"Member\"}");
	AddRouteInfo(routes, "/addgroupmember", "POST", "Groups", "Add a group member.", "{\"leader\":\"Leader\",\"member\":\"Member\"}");
	AddRouteInfo(routes, "/removegroupmember", "POST", "Groups", "Remove a group member.", "{\"name\":\"Member\"}");
	AddRouteInfo(routes, "/disbandgroup", "POST", "Groups", "Disband a group.", "{\"group_id\":1}");
	AddRouteInfo(routes, "/createguild", "POST", "Guilds", "Create a guild.", "{\"guild_name\":\"Guild\",\"leader_name\":\"Leader\"}");
	AddRouteInfo(routes, "/addguildmember", "POST", "Guilds", "Add a guild member.", "{\"guild_id\":1,\"character_id\":1,\"invited_by\":\"Leader\"}");
	AddRouteInfo(routes, "/removeguildmember", "POST", "Guilds", "Remove a guild member.", "{\"guild_id\":1,\"character_id\":1,\"removed_by\":\"Leader\"}");
	AddRouteInfo(routes, "/setguildpermission", "POST", "Guilds", "Set guild rank permission data.", "{}");
	AddRouteInfo(routes, "/setguildeventfilter", "POST", "Guilds", "Set guild event filter data.", "{}");
	AddRouteInfo(routes, "/addseller", "POST", "Broker", "Add broker seller session data.", "{}");
	AddRouteInfo(routes, "/removeseller", "POST", "Broker", "Remove broker seller session data.", "{}");
	AddRouteInfo(routes, "/additemsale", "POST", "Broker", "Add broker sale item.", "{}");
	AddRouteInfo(routes, "/removeitemsale", "POST", "Broker", "Remove broker sale item.", "{}");
	AddRouteInfo(routes, "/addplayerhouse", "POST", "Housing", "Add player house data.", "{}");
	AddRouteInfo(routes, "/updatehousedeposit", "POST", "Housing", "Update player house escrow/deposit data.", "{}");
	root.add_child("routes", routes);
}

static const char* DashboardHtml() {
	return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EQ2Emu World Dashboard</title>
<style>
:root{color-scheme:light;--bg:#f7f7f4;--panel:#fff;--ink:#17211b;--muted:#5e6962;--line:#cbd3cc;--accent:#246b55;--accent2:#365f91;--warn:#9a5b00;--danger:#a43838;--focus:#111}
*{box-sizing:border-box}
body{margin:0;font-family:Arial,Helvetica,sans-serif;background:var(--bg);color:var(--ink);line-height:1.4}
header{background:#123127;color:#fff;padding:16px 24px;border-bottom:4px solid #b9882c}
header h1{margin:0;font-size:1.35rem}
main{max-width:1380px;margin:0 auto;padding:20px}
a{color:var(--accent2)}
button,input,select,textarea{font:inherit}
button{border:1px solid #1d5544;background:var(--accent);color:#fff;border-radius:6px;padding:8px 12px;font-weight:700;cursor:pointer}
button.secondary{background:#fff;color:var(--accent)}
button.warning{background:var(--warn);border-color:#784600}
button:focus,a:focus,input:focus,textarea:focus,select:focus,[tabindex]:focus{outline:3px solid var(--focus);outline-offset:2px}
input,select,textarea{width:100%;border:1px solid #9aa6a0;border-radius:6px;padding:8px 10px;background:#fff;color:var(--ink)}
textarea{min-height:120px;resize:vertical;font-family:Consolas,Monaco,monospace}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-bottom:16px}
.card,.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}
.card strong{display:block;font-size:1.5rem}
.muted{color:var(--muted)}
.toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:end;margin:10px 0}
.toolbar>*{flex:0 1 auto}
.tabs{display:flex;gap:6px;flex-wrap:wrap;border-bottom:1px solid var(--line);margin:10px 0 16px}
.tab{background:#fff;color:var(--ink);border-color:var(--line);border-bottom-left-radius:0;border-bottom-right-radius:0}
.tab[aria-selected=true]{background:var(--accent2);color:#fff;border-color:var(--accent2)}
.view{display:none}
.view.active{display:block}
.table-wrap{overflow:auto;border:1px solid var(--line);border-radius:8px;background:#fff}
table{width:100%;border-collapse:collapse;min-width:760px}
th,td{padding:8px 10px;border-bottom:1px solid #e2e6e2;text-align:left;vertical-align:top}
th{background:#eef2ee;position:sticky;top:0;z-index:1}
pre{white-space:pre-wrap;overflow:auto;background:#111815;color:#eef9f1;padding:12px;border-radius:8px;max-height:460px}
.route-grid{display:grid;grid-template-columns:minmax(260px,380px) minmax(0,1fr);gap:14px}
.route-list{max-height:680px;overflow:auto}
.route-item{display:block;width:100%;text-align:left;margin:0 0 7px;background:#fff;color:var(--ink);border-color:var(--line)}
.route-item.active{border-color:var(--accent2);box-shadow:inset 4px 0 0 var(--accent2)}
.pill{display:inline-block;padding:2px 7px;border-radius:999px;background:#e7ece8;border:1px solid #ccd6d0;font-size:.86rem}
.status{min-height:24px;color:var(--muted)}
@media(max-width:900px){.route-grid{grid-template-columns:1fr}main{padding:14px}table{min-width:640px}}
</style>
</head>
<body>
<header><h1>EQ2Emu World Dashboard</h1></header>
<main>
  <div class="toolbar">
    <a href="/content"><button type="button">Content Workbench</button></a>
    <button class="secondary" id="refresh">Refresh</button>
    <a href="/version"><button class="secondary" type="button">Version JSON</button></a>
    <a href="/status"><button class="secondary" type="button">Status JSON</button></a>
  </div>
  <div id="summary" class="grid"></div>
  <div class="tabs" role="tablist" aria-label="Dashboard sections">
    <button class="tab" role="tab" aria-selected="true" aria-controls="overview" id="tab-overview">Overview</button>
    <button class="tab" role="tab" aria-selected="false" aria-controls="clients" id="tab-clients">Clients</button>
    <button class="tab" role="tab" aria-selected="false" aria-controls="zones" id="tab-zones">Zones</button>
    <button class="tab" role="tab" aria-selected="false" aria-controls="routes" id="tab-routes">Routes</button>
  </div>
  <section id="overview" class="view active" role="tabpanel" aria-labelledby="tab-overview"></section>
  <section id="clients" class="view" role="tabpanel" aria-labelledby="tab-clients"></section>
  <section id="zones" class="view" role="tabpanel" aria-labelledby="tab-zones"></section>
  <section id="routes" class="view" role="tabpanel" aria-labelledby="tab-routes"></section>
  <div id="statusLine" class="status" role="status" aria-live="polite"></div>
</main>
<script>
const $ = id => document.getElementById(id);
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let state = { version: null, status: null, clients: [], zones: [], routes: [], selectedRoute: null };

async function requestJson(url, options = {}, timeoutMs = 12000) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(url, { ...options, signal: controller.signal });
    const text = await response.text();
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}: ${text.slice(0, 200)}`);
    try { return JSON.parse(text); } catch { throw new Error(`Route did not return JSON: ${text.slice(0, 200)}`); }
  } finally {
    clearTimeout(timeout);
  }
}

function table(headers, rows) {
  return `<div class="table-wrap"><table><thead><tr>${headers.map(h => `<th scope="col">${esc(h)}</th>`).join('')}</tr></thead><tbody>${rows.join('') || `<tr><td colspan="${headers.length}">No rows found.</td></tr>`}</tbody></table></div>`;
}

function metric(label, value) {
  return `<div class="card"><strong>${esc(value ?? 0)}</strong><span class="muted">${esc(label)}</span></div>`;
}

async function refresh() {
  $('statusLine').textContent = 'Loading dashboard data...';
  try {
    const [version, status, clients, zones, routes] = await Promise.all([
      requestJson('/version'),
      requestJson('/status'),
      requestJson('/clients'),
      requestJson('/zones'),
      requestJson('/routes')
    ]);
    state.version = version;
    state.status = status;
    state.clients = clients.Clients || [];
    state.zones = zones.Zones || [];
    state.routes = routes.routes || [];
    state.selectedRoute = state.routes[0] || null;
    render();
    $('statusLine').textContent = 'Dashboard data loaded.';
  } catch (err) {
    $('statusLine').textContent = String(err);
  }
}

function render() {
  const s = state.status || {};
  $('summary').innerHTML = [
    metric('World', s.world_status || 'unknown'),
    metric('Players', s.player_count),
    metric('Clients', s.client_count),
    metric('Active Zones', s.zones_connected),
    metric('Login', s.login_connected || 'unknown'),
    metric('Reloading', s.world_reloading || 'unknown')
  ].join('');
  $('overview').innerHTML = `<div class="panel"><h2>Status</h2><pre>${esc(JSON.stringify({ version: state.version, status: state.status }, null, 2))}</pre></div>`;
  $('clients').innerHTML = `<div class="panel"><h2>Clients</h2>${table(['Character','Zone','Level','Status','Account','Version'], state.clients.map(c => `<tr><td>${esc(c.character_name)}</td><td>${esc(c.zonename)}</td><td>${esc(c.level)}</td><td>${esc(c.status)}</td><td>${esc(c.account_id)}</td><td>${esc(c.version)}</td></tr>`))}</div>`;
  $('zones').innerHTML = `<div class="panel"><h2>Zones</h2>${table(['Zone','File','ID','Instance','Players','Lock'], state.zones.map(z => `<tr><td>${esc(z.zone_name)}</td><td>${esc(z.zone_file_name)}</td><td>${esc(z.zone_id)}</td><td>${esc(z.instance_id)}</td><td>${esc(z.num_players)}</td><td>${esc(z.lock_state)}</td></tr>`))}</div>`;
  renderRoutes();
}

function renderRoutes() {
  const groups = [...new Set(state.routes.map(r => r.group))];
  const selected = state.selectedRoute;
  $('routes').innerHTML = `<div class="route-grid">
    <div class="panel route-list">
      <label for="routeFilter">Filter</label>
      <input id="routeFilter" placeholder="status, content, guilds">
      <div id="routeButtons"></div>
    </div>
    <div class="panel">
      <h2 id="routeTitle">${selected ? esc(selected.route) : 'Route'}</h2>
      <p><span class="pill">${selected ? esc(selected.method) : ''}</span> <span class="pill">${selected ? esc(selected.group) : ''}</span></p>
      <p class="muted">${selected ? esc(selected.description) : ''}</p>
      <label for="payload">JSON Payload</label>
      <textarea id="payload">${selected ? esc(selected.payload || '{}') : '{}'}</textarea>
      <div class="toolbar">
        <button id="runRoute">Run Route</button>
        ${selected && selected.method === 'GET' ? `<a id="openRoute" href="${esc(selected.route)}"><button class="secondary" type="button">Open Route</button></a>` : '<button class="secondary" type="button" disabled>Open Route</button>'}
      </div>
      <pre id="routeOutput">No route run yet.</pre>
    </div>
  </div>`;
  const drawButtons = () => {
    const filter = $('routeFilter').value.toLowerCase();
    $('routeButtons').innerHTML = groups.map(group => {
      const routes = state.routes.filter(r => r.group === group && (`${r.route} ${r.description} ${r.group}`).toLowerCase().includes(filter));
      if (!routes.length) return '';
      return `<h3>${esc(group)}</h3>${routes.map(r => `<button class="route-item ${selected && selected.route === r.route ? 'active' : ''}" data-route="${esc(r.route)}"><span class="pill">${esc(r.method)}</span> ${esc(r.route)}<br><span class="muted">${esc(r.description)}</span></button>`).join('')}`;
    }).join('');
    document.querySelectorAll('[data-route]').forEach(button => button.addEventListener('click', () => {
      state.selectedRoute = state.routes.find(r => r.route === button.dataset.route);
      renderRoutes();
    }));
  };
  drawButtons();
  $('routeFilter').addEventListener('input', drawButtons);
  $('runRoute').addEventListener('click', runSelectedRoute);
}

async function runSelectedRoute() {
  const route = state.selectedRoute;
  if (!route) return;
  if (route.group === 'UI' && route.route !== '/routes') {
    $('routeOutput').textContent = 'Open this browser page with the Open Route button.';
    return;
  }
  $('routeOutput').textContent = 'Running...';
  try {
    const options = route.method === 'POST'
      ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: $('payload').value || '{}' }
      : {};
    const data = await requestJson(route.route, options);
    $('routeOutput').textContent = JSON.stringify(data, null, 2);
  } catch (err) {
    $('routeOutput').textContent = String(err);
  }
}

document.querySelectorAll('.tab').forEach(tab => tab.addEventListener('click', () => {
  document.querySelectorAll('.tab').forEach(t => t.setAttribute('aria-selected', 'false'));
  document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
  tab.setAttribute('aria-selected', 'true');
  $(tab.getAttribute('aria-controls')).classList.add('active');
}));
$('refresh').addEventListener('click', refresh);
refresh();
</script>
</body>
</html>)HTML";
}

static bool RunInsert(Query& query, const std::string& sql) {
	query.RunQuery2(sql, Q_INSERT);
	return query.GetErrorNumber() == 0;
}

static int32 NextGroundSpawnID();

static void ApplyNpc(const ptree& body, ptree& response) {
	ptree plan;
	int32 zone_id = GetInt(body, "zone_id", 0);
	std::string name = GetString(body, "name", "new content NPC");
	int32 model_type = GetInt(body, "model_type", 0);
	int32 level = std::max<int32>(1, GetInt(body, "level", 1));
	int32 size = std::max<int32>(1, GetInt(body, "size", 32));
	int32 count = std::max<int32>(1, std::min<int32>(25, GetInt(body, "count", 1)));
	float radius = std::max<float>(0.0f, GetFloat(body, "radius", 4.0f));
	float x = GetFloat(body, "x", 0.0f);
	float y = GetFloat(body, "y", 0.0f);
	float z = GetFloat(body, "z", 0.0f);
	float heading = GetFloat(body, "heading", 0.0f);
	int32 faction_id = GetInt(body, "faction_id", 0);
	int32 respawn = std::max<int32>(0, GetInt(body, "respawn", 300));
	int32 heroic = GetInt(body, "heroic_flag", 0);
	float aggro = GetFloat(body, "aggro_radius", GetBool(body, "attackable", true) ? 10.0f : 0.0f);
	bool attackable = GetBool(body, "attackable", true);
	int32 loot_table_id = GetInt(body, "loot_table_id", 0);
	bool dry_run = GetBool(body, "dry_run", true);

	AddPlan(plan, "Create one spawn row named \"" + name + "\".");
	AddPlan(plan, "Create spawn_npcs stats at level " + std::to_string(level) + ".");
	AddPlan(plan, "Create one spawn location with " + std::to_string(count) + " placement point(s) in zone " + std::to_string(zone_id) + ".");
	if (loot_table_id > 0)
		AddPlan(plan, "Attach loot table " + std::to_string(loot_table_id) + ".");

	response.add_child("plan", plan);
	if (dry_run) {
		response.put("success", 1);
		response.put("dry_run", 1);
		return;
	}

	if (zone_id <= 0 || name.empty()) {
		response.put("success", 0);
		response.put("error", "Zone and NPC name are required.");
		return;
	}

	std::string escaped_name = EscapeSql(name);
	Query spawn_query;
	std::ostringstream spawn_sql;
	spawn_sql << "INSERT INTO spawn (name, race, model_type, size, targetable, show_name, attackable, show_level, show_command_icon, display_hand_icon, faction_id, collision_radius, hp, power) VALUES ('"
		<< escaped_name << "', 0, " << model_type << ", " << size << ", 1, 1, " << (attackable ? 1 : 0) << ", " << (attackable ? 1 : 0)
		<< ", " << (attackable ? 1 : 0) << ", 0, " << faction_id << ", 32, " << (level * 25 + 1) << ", " << (level * 25 + 1) << ")";
	if (!RunInsert(spawn_query, spawn_sql.str())) {
		response.put("success", 0);
		response.put("error", spawn_query.GetError());
		return;
	}
	int32 spawn_id = spawn_query.GetLastInsertedID();

	Query npc_query;
	std::ostringstream npc_sql;
	npc_sql << "INSERT INTO spawn_npcs (spawn_id, min_level, max_level, enc_level, class_, gender, heroic_flag, aggro_radius, ai_strategy, attack_type) VALUES ("
		<< spawn_id << ", " << level << ", " << level << ", " << level << ", 1, 0, " << heroic << ", " << aggro << ", 'BALANCED', 0)";
	if (!RunInsert(npc_query, npc_sql.str())) {
		response.put("success", 0);
		response.put("error", npc_query.GetError());
		return;
	}

	Query loc_name_query;
	std::string loc_name = name + " content placement";
	loc_name_query.RunQuery2(Q_INSERT, "INSERT INTO spawn_location_name (name) VALUES ('%s')", EscapeSql(loc_name).c_str());
	if (loc_name_query.GetErrorNumber() != 0) {
		response.put("success", 0);
		response.put("error", loc_name_query.GetError());
		return;
	}
	int32 spawn_location_id = loc_name_query.GetLastInsertedID();

	Query entry_query;
	entry_query.RunQuery2(Q_INSERT, "INSERT INTO spawn_location_entry (spawn_id, spawn_location_id, spawnpercentage, `condition`) VALUES (%u, %u, 100, 0)", spawn_id, spawn_location_id);
	if (entry_query.GetErrorNumber() != 0) {
		response.put("success", 0);
		response.put("error", entry_query.GetError());
		return;
	}

	for (int32 i = 0; i < count; ++i) {
		float px = x;
		float pz = z;
		if (count > 1) {
			float angle = static_cast<float>((6.28318530718 * i) / count);
			px += std::cos(angle) * radius;
			pz += std::sin(angle) * radius;
		}
		Query placement_query;
		placement_query.RunQuery2(Q_INSERT,
			"INSERT INTO spawn_location_placement (zone_id, spawn_location_id, x, y, z, heading, respawn, duplicated_spawn) VALUES (%u, %u, %f, %f, %f, %f, %u, 1)",
			zone_id, spawn_location_id, px, y, pz, heading, respawn);
		if (placement_query.GetErrorNumber() != 0) {
			response.put("success", 0);
			response.put("error", placement_query.GetError());
			return;
		}
	}

	if (loot_table_id > 0) {
		Query loot_query;
		loot_query.RunQuery2(Q_INSERT, "INSERT INTO spawn_loot (spawn_id, loottable_id) VALUES (%u, %u)", spawn_id, loot_table_id);
	}

	ptree created;
	created.put("spawn_id", spawn_id);
	created.put("spawn_location_id", spawn_location_id);
	response.add_child("created", created);
	response.put("success", 1);
	response.put("dry_run", 0);
}

static void ApplyHarvest(const ptree& body, ptree& response) {
	ptree plan;
	int32 zone_id = GetInt(body, "zone_id", 0);
	std::string name = GetString(body, "name", "resource node");
	std::string skill = WhitelistHarvestSkill(GetString(body, "collection_skill", "Gathering"));
	int32 min_skill = std::max<int32>(1, GetInt(body, "min_skill_level", 1));
	int32 min_level = std::max<int32>(0, GetInt(body, "min_adventure_level", 0));
	int32 count = std::max<int32>(1, std::min<int32>(50, GetInt(body, "count", 6)));
	float radius = std::max<float>(0.0f, GetFloat(body, "radius", 12.0f));
	float x = GetFloat(body, "x", 0.0f);
	float y = GetFloat(body, "y", 0.0f);
	float z = GetFloat(body, "z", 0.0f);
	int32 respawn = std::max<int32>(0, GetInt(body, "respawn", 180));
	std::vector<int32> item_ids = ParseIdList(GetString(body, "item_ids", ""));
	bool dry_run = GetBool(body, "dry_run", true);

	AddPlan(plan, "Create a groundspawn definition for " + skill + ".");
	AddPlan(plan, "Create one harvest spawn and " + std::to_string(count) + " placement point(s).");
	AddPlan(plan, "Attach " + std::to_string(item_ids.size()) + " harvest item row(s).");
	response.add_child("plan", plan);
	if (dry_run) {
		response.put("success", 1);
		response.put("dry_run", 1);
		return;
	}
	if (zone_id <= 0 || item_ids.empty()) {
		response.put("success", 0);
		response.put("error", "Zone and at least one item id are required.");
		return;
	}

	int32 groundspawn_id = NextGroundSpawnID();
	if (groundspawn_id <= 0) {
		response.put("success", 0);
		response.put("error", "Unable to allocate the next groundspawn id.");
		return;
	}

	Query ground_query;
	ground_query.RunQuery2(Q_INSERT,
		"INSERT INTO groundspawns (groundspawn_id, min_skill_level, min_adventure_level, tablename) VALUES (%u, %u, %u, '%s')",
		groundspawn_id, min_skill, min_level, EscapeSql(name).c_str());
	if (ground_query.GetErrorNumber() != 0) {
		response.put("success", 0);
		response.put("error", ground_query.GetError());
		return;
	}

	Query spawn_query;
	spawn_query.RunQuery2(Q_INSERT,
		"INSERT INTO spawn (name, race, model_type, size, targetable, show_name, attackable, show_level, display_hand_icon, collision_radius) VALUES ('%s', 0, 0, 32, 1, 0, 0, 0, 1, 16)",
		EscapeSql(name).c_str());
	if (spawn_query.GetErrorNumber() != 0) {
		response.put("success", 0);
		response.put("error", spawn_query.GetError());
		return;
	}
	int32 spawn_id = spawn_query.GetLastInsertedID();

	Query spawn_ground_query;
	spawn_ground_query.RunQuery2(Q_INSERT,
		"INSERT INTO spawn_ground (spawn_id, number_harvests, num_attempts_per_harvest, groundspawn_id, collection_skill, randomize_heading) VALUES (%u, 3, 1, %u, '%s', 1)",
		spawn_id, groundspawn_id, EscapeSql(skill).c_str());
	if (spawn_ground_query.GetErrorNumber() != 0) {
		response.put("success", 0);
		response.put("error", spawn_ground_query.GetError());
		return;
	}

	float percent = item_ids.empty() ? 100.0f : (100.0f / item_ids.size());
	for (int32 item_id : item_ids) {
		Query item_query;
		item_query.RunQuery2(Q_INSERT,
			"INSERT INTO groundspawn_items (groundspawn_id, item_id, is_rare, grid_id, percent) VALUES (%u, %u, 0, 0, %f)",
			groundspawn_id, item_id, percent);
	}

	Query loc_name_query;
	loc_name_query.RunQuery2(Q_INSERT, "INSERT INTO spawn_location_name (name) VALUES ('%s')", EscapeSql(name + " harvest placement").c_str());
	int32 spawn_location_id = loc_name_query.GetLastInsertedID();

	Query entry_query;
	entry_query.RunQuery2(Q_INSERT, "INSERT INTO spawn_location_entry (spawn_id, spawn_location_id, spawnpercentage, `condition`) VALUES (%u, %u, 100, 0)", spawn_id, spawn_location_id);

	for (int32 i = 0; i < count; ++i) {
		float angle = count > 1 ? static_cast<float>((6.28318530718 * i) / count) : 0.0f;
		float px = x + (count > 1 ? std::cos(angle) * radius : 0.0f);
		float pz = z + (count > 1 ? std::sin(angle) * radius : 0.0f);
		Query placement_query;
		placement_query.RunQuery2(Q_INSERT,
			"INSERT INTO spawn_location_placement (zone_id, spawn_location_id, x, y, z, heading, respawn, duplicated_spawn) VALUES (%u, %u, %f, %f, %f, 0, %u, 1)",
			zone_id, spawn_location_id, px, y, pz, respawn);
	}

	ptree created;
	created.put("spawn_id", spawn_id);
	created.put("groundspawn_id", groundspawn_id);
	created.put("spawn_location_id", spawn_location_id);
	response.add_child("created", created);
	response.put("success", 1);
	response.put("dry_run", 0);
}

static void ApplyPoi(const ptree& body, ptree& response) {
	ptree plan;
	int32 zone_id = GetInt(body, "zone_id", 0);
	std::string name = GetString(body, "name", "Point of Interest");
	float x = GetFloat(body, "x", 0.0f);
	float y = GetFloat(body, "y", 0.0f);
	float z = GetFloat(body, "z", 0.0f);
	int32 grid_id = GetInt(body, "grid_id", 0);
	bool include_y = GetBool(body, "include_y", false);
	bool discovery = GetBool(body, "discovery", true);
	bool dry_run = GetBool(body, "dry_run", true);

	AddPlan(plan, "Create one locations row named \"" + name + "\".");
	AddPlan(plan, "Create one location_details point at the supplied coordinates.");
	response.add_child("plan", plan);
	if (dry_run) {
		response.put("success", 1);
		response.put("dry_run", 1);
		return;
	}
	if (zone_id <= 0 || name.empty()) {
		response.put("success", 0);
		response.put("error", "Zone and POI name are required.");
		return;
	}

	Query location_query;
	location_query.RunQuery2(Q_INSERT,
		"INSERT INTO locations (zone_id, grid_id, name, include_y, discovery) VALUES (%u, %u, '%s', %u, %u)",
		zone_id, grid_id, EscapeSql(name).c_str(), include_y ? 1 : 0, discovery ? 1 : 0);
	if (location_query.GetErrorNumber() != 0) {
		response.put("success", 0);
		response.put("error", location_query.GetError());
		return;
	}
	int32 location_id = location_query.GetLastInsertedID();

	Query detail_query;
	detail_query.RunQuery2(Q_INSERT,
		"INSERT INTO location_details (location_id, x, y, z) VALUES (%u, %f, %f, %f)",
		location_id, x, y, z);

	ptree created;
	created.put("location_id", location_id);
	response.add_child("created", created);
	response.put("success", 1);
	response.put("dry_run", 0);
}

static int32 NextItemID() {
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT, "SELECT COALESCE(MAX(id), 0) + 1 FROM items");
	if (result && (row = mysql_fetch_row(result)))
		return RowInt(row, 0);
	return 0;
}

static int32 NextGroundSpawnID() {
	Query query;
	MYSQL_ROW row;
	MYSQL_RES* result = query.RunQuery2(Q_SELECT, "SELECT COALESCE(MAX(groundspawn_id), 0) + 1 FROM groundspawns");
	if (result && (row = mysql_fetch_row(result)))
		return RowInt(row, 0);
	return 0;
}

static void ApplyLoot(const ptree& body, ptree& response) {
	ptree plan;
	std::string item_name = GetString(body, "item_name", "New Item");
	std::string item_type = WhitelistItemType(GetString(body, "item_type", "Normal"));
	std::string description = GetString(body, "description", "");
	int32 item_id = GetInt(body, "item_id", 0);
	int32 tier = std::max<int32>(1, GetInt(body, "tier", 1));
	int32 level = std::max<int32>(1, GetInt(body, "level", 1));
	int32 icon = GetInt(body, "icon", 0);
	int32 spawn_id = GetInt(body, "spawn_id", 0);
	int32 loottable_id = GetInt(body, "loottable_id", 0);
	std::string loottable_name = GetString(body, "loottable_name", item_name + " loot");
	float drop_probability = GetFloat(body, "drop_probability", 25.0f);
	bool create_item = GetBool(body, "create_item", true);
	bool dry_run = GetBool(body, "dry_run", true);

	AddPlan(plan, create_item ? "Create a new item row." : "Use existing item id " + std::to_string(item_id) + ".");
	AddPlan(plan, loottable_id > 0 ? "Add item to existing loot table " + std::to_string(loottable_id) + "." : "Create a new loot table and add a lootdrop row.");
	if (spawn_id > 0)
		AddPlan(plan, "Attach the loot table to spawn " + std::to_string(spawn_id) + ".");
	response.add_child("plan", plan);
	if (dry_run) {
		response.put("success", 1);
		response.put("dry_run", 1);
		return;
	}

	if (create_item) {
		item_id = NextItemID();
		if (item_id <= 0) {
			response.put("success", 0);
			response.put("error", "Unable to allocate the next item id.");
			return;
		}
		Query item_query;
		item_query.RunQuery2(Q_INSERT,
			"INSERT INTO items (id, name, item_type, icon, developer_notes, tier, description, recommended_level, adventure_default_level, required_level) "
			"VALUES (%u, '%s', '%s', %u, 'Created by content workbench', %u, '%s', %u, %u, %u)",
			item_id, EscapeSql(item_name).c_str(), EscapeSql(item_type).c_str(), icon, tier, EscapeSql(description).c_str(), level, level, level);
		if (item_query.GetErrorNumber() != 0) {
			response.put("success", 0);
			response.put("error", item_query.GetError());
			return;
		}
	}
	if (item_id <= 0) {
		response.put("success", 0);
		response.put("error", "An item id is required.");
		return;
	}

	if (loottable_id <= 0) {
		Query table_query;
		table_query.RunQuery2(Q_INSERT,
			"INSERT INTO loottable (name, mincoin, maxcoin, maxlootitems, lootdrop_probability, coin_probability) VALUES ('%s', 0, 0, 1, 100, 0)",
			EscapeSql(loottable_name).c_str());
		if (table_query.GetErrorNumber() != 0) {
			response.put("success", 0);
			response.put("error", table_query.GetError());
			return;
		}
		loottable_id = table_query.GetLastInsertedID();
	}

	Query drop_query;
	drop_query.RunQuery2(Q_INSERT,
		"INSERT INTO lootdrop (loot_table_id, item_id, item_charges, equip_item, probability, no_drop_quest_completed) VALUES (%u, %u, 1, 0, %f, 0)",
		loottable_id, item_id, drop_probability);
	if (drop_query.GetErrorNumber() != 0) {
		response.put("success", 0);
		response.put("error", drop_query.GetError());
		return;
	}

	if (spawn_id > 0) {
		Query spawn_loot_query;
		spawn_loot_query.RunQuery2(Q_INSERT, "INSERT INTO spawn_loot (spawn_id, loottable_id) VALUES (%u, %u)", spawn_id, loottable_id);
	}

	ptree created;
	created.put("item_id", item_id);
	created.put("loottable_id", loottable_id);
	response.add_child("created", created);
	response.put("success", 1);
	response.put("dry_run", 0);
}

static void ApplyDialog(const ptree& body, ptree& response) {
	ptree plan;
	int32 spawn_id = GetInt(body, "spawn_id", 0);
	std::string npc_name = GetString(body, "npc_name", "npc");
	bool write_file = GetBool(body, "write_file", true);
	bool attach_script = GetBool(body, "attach_script", true);
	bool dry_run = GetBool(body, "dry_run", true);
	std::string script_name = std::to_string(spawn_id) + "_" + Slugify(npc_name) + ".lua";
	std::string script_path = "SpawnScripts/ContentWorkbench/" + script_name;
	std::string lua = BuildDialogLua(body, script_name);

	AddPlan(plan, "Generate a hail conversation Lua script.");
	if (write_file)
		AddPlan(plan, "Write " + script_path + ".");
	if (attach_script)
		AddPlan(plan, "Attach the script to spawn " + std::to_string(spawn_id) + " in spawn_scripts.");
	response.add_child("plan", plan);
	response.put("lua", lua);
	response.put("script_path", script_path);

	if (dry_run) {
		response.put("success", 1);
		response.put("dry_run", 1);
		return;
	}
	if (spawn_id <= 0) {
		response.put("success", 0);
		response.put("error", "A spawn id is required.");
		return;
	}

	if (write_file) {
		boost::filesystem::path dir("SpawnScripts/ContentWorkbench");
		boost::filesystem::create_directories(dir);
		std::ofstream file((dir / script_name).string().c_str(), std::ios::out | std::ios::trunc);
		if (!file.is_open()) {
			response.put("success", 0);
			response.put("error", "Unable to write " + script_path + ". Ensure the world server working directory is writable.");
			return;
		}
		file << lua;
		file.close();
	}

	if (attach_script) {
		Query delete_query;
		delete_query.RunQuery2(Q_DELETE, "DELETE FROM spawn_scripts WHERE spawn_id=%u AND spawnentry_id=0 AND spawn_location_id=0", spawn_id);
		Query insert_query;
		insert_query.RunQuery2(Q_INSERT,
			"INSERT INTO spawn_scripts (spawn_id, spawnentry_id, spawn_location_id, lua_script) VALUES (%u, 0, 0, '%s')",
			spawn_id, EscapeSql(script_path).c_str());
		if (insert_query.GetErrorNumber() != 0) {
			response.put("success", 0);
			response.put("error", insert_query.GetError());
			return;
		}
	}

	response.put("success", 1);
	response.put("dry_run", 0);
}

static const char* ContentWorkbenchHtml() {
	return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EQ2Emu Content Workbench</title>
<style>
:root {
  color-scheme: light;
  --bg: #f7f7f4;
  --panel: #ffffff;
  --ink: #17211b;
  --muted: #5e6962;
  --line: #cbd3cc;
  --accent: #246b55;
  --accent-2: #365f91;
  --warn: #9a5b00;
  --danger: #a43838;
  --ok: #267247;
  --focus: #111111;
}
* { box-sizing: border-box; }
body { margin: 0; font-family: Arial, Helvetica, sans-serif; color: var(--ink); background: var(--bg); line-height: 1.4; }
a { color: var(--accent-2); }
header { background: #123127; color: #fff; padding: 14px 22px; border-bottom: 4px solid #b9882c; }
header h1 { margin: 0; font-size: 1.35rem; letter-spacing: 0; }
header p { margin: 4px 0 0; color: #dce7e1; }
main { display: grid; grid-template-columns: 320px minmax(0, 1fr); min-height: calc(100vh - 75px); }
aside { border-right: 1px solid var(--line); padding: 18px; background: #eef2ee; }
section.workspace { padding: 18px 22px 48px; }
label { display: block; font-weight: 700; margin: 12px 0 4px; }
input, select, textarea, button { font: inherit; }
input, select, textarea { width: 100%; border: 1px solid #9aa6a0; border-radius: 6px; padding: 8px 10px; background: #fff; color: var(--ink); }
textarea { min-height: 92px; resize: vertical; }
button { border: 1px solid #1d5544; background: var(--accent); color: #fff; border-radius: 6px; padding: 8px 12px; cursor: pointer; font-weight: 700; }
button.secondary { background: #fff; color: var(--accent); }
button.warning { background: var(--warn); border-color: #784600; }
button:focus, input:focus, select:focus, textarea:focus, [tabindex]:focus { outline: 3px solid var(--focus); outline-offset: 2px; }
button:disabled { opacity: .55; cursor: not-allowed; }
.row { display: flex; gap: 10px; align-items: end; flex-wrap: wrap; }
.row > * { flex: 1 1 150px; }
.toolbar { display: flex; gap: 8px; flex-wrap: wrap; margin: 12px 0; }
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(135px, 1fr)); gap: 10px; margin: 12px 0 18px; }
.metric { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 12px; }
.metric strong { display: block; font-size: 1.45rem; }
.tabs { display: flex; flex-wrap: wrap; gap: 6px; border-bottom: 1px solid var(--line); margin-top: 10px; }
.tab { background: #fff; color: var(--ink); border-color: var(--line); border-bottom-left-radius: 0; border-bottom-right-radius: 0; }
.tab[aria-selected="true"] { background: var(--accent-2); color: #fff; border-color: var(--accent-2); }
.panel { display: none; padding-top: 16px; }
.panel.active { display: block; }
.box { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 14px; margin-bottom: 14px; }
.box h2, .box h3 { margin: 0 0 10px; font-size: 1.1rem; }
.table-wrap { overflow: auto; border: 1px solid var(--line); border-radius: 8px; background: #fff; }
table { width: 100%; border-collapse: collapse; min-width: 640px; }
th, td { padding: 8px 10px; border-bottom: 1px solid #e2e6e2; text-align: left; vertical-align: top; }
th { background: #eef2ee; position: sticky; top: 0; z-index: 1; }
.status { min-height: 24px; margin-top: 10px; color: var(--muted); }
.issue-error { border-left: 5px solid var(--danger); }
.issue-warning { border-left: 5px solid var(--warn); }
.issue-info { border-left: 5px solid var(--accent-2); }
.issue-ok { border-left: 5px solid var(--ok); }
.pill { display: inline-block; padding: 2px 7px; border-radius: 999px; background: #e7ece8; border: 1px solid #ccd6d0; font-size: .88rem; }
.map-wrap { background: #fff; border: 1px solid var(--line); border-radius: 8px; padding: 8px; }
svg { width: 100%; height: 560px; display: block; background: #fbfcfa; border: 1px solid #d6ded8; }
.legend { display: flex; gap: 12px; flex-wrap: wrap; margin: 8px 0; }
.legend label { display: inline-flex; align-items: center; gap: 6px; margin: 0; font-weight: 400; }
.legend input { width: auto; }
pre { white-space: pre-wrap; overflow: auto; background: #111815; color: #eef9f1; padding: 12px; border-radius: 8px; }
.help { color: var(--muted); font-size: .95rem; }
.sr-only { position: absolute; width: 1px; height: 1px; padding: 0; margin: -1px; overflow: hidden; clip: rect(0,0,0,0); border: 0; }
@media (max-width: 900px) {
  main { grid-template-columns: 1fr; }
  aside { border-right: 0; border-bottom: 1px solid var(--line); }
  svg { height: 420px; }
}
</style>
</head>
<body>
<header>
  <h1>EQ2Emu Content Workbench</h1>
</header>
<main>
  <aside aria-label="Workbench controls">
    <label for="zoneSelect">Zone</label>
    <select id="zoneSelect"></select>
    <div class="toolbar">
      <button id="loadZone">Load Zone</button>
      <button class="secondary" id="refreshAll">Refresh</button>
    </div>
    <label for="editorSelect">Online editor position</label>
    <select id="editorSelect"></select>
    <button class="secondary" id="useEditor">Use Position In Forms</button>
    <div id="sideStatus" class="status" role="status" aria-live="polite"></div>
  </aside>
  <section class="workspace">
    <div class="box">
      <h2 id="zoneTitle">Choose a zone</h2>
      <p id="zoneDescription" class="help"></p>
      <div id="metrics" class="cards"></div>
    </div>
    <div class="tabs" role="tablist" aria-label="Content workbench sections">
      <button class="tab" role="tab" aria-selected="true" aria-controls="overview" id="tab-overview">Overview</button>
      <button class="tab" role="tab" aria-selected="false" aria-controls="map" id="tab-map">Map</button>
      <button class="tab" role="tab" aria-selected="false" aria-controls="audit" id="tab-audit">Audit</button>
      <button class="tab" role="tab" aria-selected="false" aria-controls="quests" id="tab-quests">Quests</button>
      <button class="tab" role="tab" aria-selected="false" aria-controls="builders" id="tab-builders">Builders</button>
    </div>
    <div id="overview" class="panel active" role="tabpanel" aria-labelledby="tab-overview"></div>
    <div id="map" class="panel" role="tabpanel" aria-labelledby="tab-map"></div>
    <div id="audit" class="panel" role="tabpanel" aria-labelledby="tab-audit"></div>
    <div id="quests" class="panel" role="tabpanel" aria-labelledby="tab-quests"></div>
    <div id="builders" class="panel" role="tabpanel" aria-labelledby="tab-builders"></div>
  </section>
</main>
<script>
const state = { zones: [], editors: [], zone: null, data: null };
const $ = id => document.getElementById(id);
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const num = value => Number(value || 0);

async function postJson(url, body = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 12000);
  try {
    const response = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal: controller.signal });
    const text = await response.text();
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}: ${text.slice(0, 200)}`);
    try { return JSON.parse(text); } catch { throw new Error(`Route did not return JSON: ${text.slice(0, 200)}`); }
  } finally {
    clearTimeout(timeout);
  }
}

function setStatus(text) { $('sideStatus').textContent = text; }

function renderOptions() {
  $('zoneSelect').innerHTML = state.zones.map(z => `<option value="${esc(z.id)}">${esc(z.name)} - ${esc(z.description)}</option>`).join('');
  $('editorSelect').innerHTML = state.editors.length
    ? state.editors.map(e => `<option value="${esc(e.character_name)}">${esc(e.character_name)} in ${esc(e.zone_name)} (${fmt(e.x)}, ${fmt(e.y)}, ${fmt(e.z)})</option>`).join('')
    : '<option value="">No online editors found</option>';
}

function fmt(value) { return Number(value || 0).toFixed(2); }

async function bootstrap() {
  setStatus('Loading zones and online editors...');
  const data = await postJson('/content/api/bootstrap');
  state.zones = data.zones || [];
  state.editors = data.editors || [];
  renderOptions();
  setStatus(`Loaded ${state.zones.length} zones and ${state.editors.length} online editor positions.`);
}

async function loadZone() {
  const zoneId = Number($('zoneSelect').value);
  if (!zoneId) return;
  setStatus('Loading zone content...');
  const data = await postJson('/content/api/zone', { zone_id: zoneId });
  state.data = data;
  state.zone = data.zone;
  renderZone();
  setStatus(`Loaded ${data.zone.name}.`);
}

function renderZone() {
  const data = state.data;
  $('zoneTitle').textContent = data.zone.name;
  $('zoneDescription').textContent = `${data.zone.description || ''} File: ${data.zone.file || 'unknown'} Safe: ${fmt(data.zone.safe_x)}, ${fmt(data.zone.safe_y)}, ${fmt(data.zone.safe_z)}`;
  renderMetrics(data.summary || {});
  renderOverview(data);
  renderMap(data);
  renderAudit(data);
  renderQuests(data);
  renderBuilders();
}

function renderMetrics(summary) {
  const metrics = [
    ['Placements', summary.placements], ['Spawns', summary.unique_spawns], ['NPCs', summary.npcs],
    ['Hostile', summary.hostile_npcs], ['Quests', summary.quests], ['Harvest', summary.groundspawns],
    ['POIs', summary.pois], ['Merchants', summary.merchants], ['Transport', summary.transporters]
  ];
  $('metrics').innerHTML = metrics.map(([label, value]) => `<div class="metric"><strong>${esc(value || 0)}</strong><span>${esc(label)}</span></div>`).join('');
}

function table(headers, rows) {
  return `<div class="table-wrap"><table><thead><tr>${headers.map(h => `<th scope="col">${esc(h)}</th>`).join('')}</tr></thead><tbody>${rows.join('') || `<tr><td colspan="${headers.length}">No rows found.</td></tr>`}</tbody></table></div>`;
}

function renderOverview(data) {
  const npcRows = (data.npcs || []).map(n => `<tr><td>${esc(n.name)}<br><span class="help">Spawn ${n.spawn_id}</span></td><td>${esc(n.min_level)}-${esc(n.max_level)}</td><td>${esc(n.model_type)}</td><td>${n.attackable == 1 ? 'Yes' : 'No'}</td><td>${esc(n.placements)}</td><td>${esc(n.loot_tables)}</td></tr>`);
  const harvestRows = (data.groundspawns || []).map(g => `<tr><td>${esc(g.name)}<br><span class="help">Spawn ${g.spawn_id}</span></td><td>${esc(g.skill)}</td><td>${esc(g.min_skill_level)}</td><td>${esc(g.item_count)}</td><td>${esc(g.placements)}</td></tr>`);
  const poiRows = (data.pois || []).map(p => `<tr><td>${esc(p.name)}<br><span class="help">Location ${p.id}</span></td><td>${esc(p.grid_id)}</td><td>${p.discovery == 1 ? 'Yes' : 'No'}</td><td>${fmt(p.x)}, ${fmt(p.y)}, ${fmt(p.z)}</td></tr>`);
  $('overview').innerHTML = `
    <div class="box"><h2>NPCs And Enemies</h2>${table(['Name', 'Level', 'Model', 'Hostile', 'Placements', 'Loot Tables'], npcRows)}</div>
    <div class="box"><h2>Harvestables</h2>${table(['Name', 'Skill', 'Min Skill', 'Items', 'Placements'], harvestRows)}</div>
    <div class="box"><h2>Points Of Interest</h2>${table(['Name', 'Grid', 'Discovery', 'Position'], poiRows)}</div>`;
}

function renderMap(data) {
  const points = data.map_points || [];
  const filters = ['npc', 'harvest', 'widget', 'sign', 'object'];
  $('map').innerHTML = `
    <div class="box">
      <h2>Zone Map Overlay</h2>
      <div class="legend">${filters.map(f => `<label><input type="checkbox" class="mapFilter" value="${f}" checked> ${f}</label>`).join('')}</div>
      <div class="map-wrap"><svg id="zoneMap" role="img" aria-label="Zone content placement map"></svg></div>
      <div id="mapReadout" class="status" role="status" aria-live="polite"></div>
    </div>`;
  document.querySelectorAll('.mapFilter').forEach(input => input.addEventListener('change', drawMap));
  drawMap();

  function drawMap() {
    const svg = $('zoneMap');
    const active = new Set(Array.from(document.querySelectorAll('.mapFilter:checked')).map(i => i.value));
    const visible = points.filter(p => active.has(p.type));
    if (!visible.length) {
      svg.innerHTML = '<text x="20" y="40">No visible points.</text>';
      return;
    }
    const xs = visible.map(p => num(p.x));
    const zs = visible.map(p => num(p.z));
    const minX = Math.min(...xs), maxX = Math.max(...xs), minZ = Math.min(...zs), maxZ = Math.max(...zs);
    const width = 1000, height = 560, pad = 32;
    const sx = x => pad + ((x - minX) / Math.max(1, maxX - minX)) * (width - pad * 2);
    const sz = z => height - pad - ((z - minZ) / Math.max(1, maxZ - minZ)) * (height - pad * 2);
    const color = p => p.type === 'npc' ? (p.attackable == 1 ? '#a43838' : '#365f91') : p.type === 'harvest' ? '#267247' : p.type === 'widget' ? '#8a5aa8' : p.type === 'sign' ? '#9a5b00' : '#545f5a';
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
    svg.innerHTML = `<rect x="1" y="1" width="${width - 2}" height="${height - 2}" fill="#fbfcfa" stroke="#d6ded8"></rect>` +
      visible.map(p => `<circle tabindex="0" role="button" aria-label="${esc(p.name)} ${esc(p.type)} at ${fmt(p.x)}, ${fmt(p.y)}, ${fmt(p.z)}" cx="${sx(num(p.x))}" cy="${sz(num(p.z))}" r="5" fill="${color(p)}" data-name="${esc(p.name)}" data-info="${esc(`${p.type} spawn ${p.spawn_id} at ${fmt(p.x)}, ${fmt(p.y)}, ${fmt(p.z)}`)}"><title>${esc(p.name)}</title></circle>`).join('');
    svg.querySelectorAll('circle').forEach(c => {
      const show = () => $('mapReadout').textContent = `${c.dataset.name}: ${c.dataset.info}`;
      c.addEventListener('click', show);
      c.addEventListener('focus', show);
    });
  }
}

function renderAudit(data) {
  const issues = data.audit || [];
  $('audit').innerHTML = `<div class="box"><h2>Content QA</h2>${issues.length ? issues.map(i => `
    <div class="box issue-${esc(i.severity)}">
      <strong>${esc(i.severity).toUpperCase()} - ${esc(i.area)} - ${esc(i.subject)}</strong>
      <p>${esc(i.detail)}</p>
      <p class="help">${esc(i.fix)}</p>
    </div>`).join('') : '<p>No audit issues found for the current checks.</p>'}</div>`;
}

function renderQuests(data) {
  const questRows = (data.quests || []).map(q => `<tr><td>${esc(q.name)}<br><span class="help">Quest ${q.quest_id}</span></td><td>${esc(q.level)}</td><td>${esc(q.starter || 'None')}</td><td>${esc(q.lua_script || '')}</td><td>${esc(q.detail_count)}</td></tr>`);
  const validationRows = (data.quest_validation || []).map(q => `<tr><td>${esc(q.name)}<br><span class="help">Quest ${q.quest_id}</span></td><td><span class="pill">${esc(q.status)}</span></td><td>${esc(q.starter_missing)}</td><td>${esc(q.missing_items)}</td><td>${esc(q.missing_prereq_quests)}</td><td>${q.script_missing == 1 ? 'Missing' : 'OK'}</td></tr>`);
  $('quests').innerHTML = `
    <div class="box"><h2>Quest Manifest</h2>${table(['Quest', 'Level', 'Starter', 'Lua Script', 'Details'], questRows)}</div>
    <div class="box"><h2>Quest Graph Validation</h2>${table(['Quest', 'Status', 'Missing Starter', 'Missing Items', 'Missing Prereq Quests', 'Script'], validationRows)}</div>`;
}

function positionFields(prefix) {
  return `<div class="row">
    <div><label for="${prefix}_x">X</label><input id="${prefix}_x" inputmode="decimal" value="0"></div>
    <div><label for="${prefix}_y">Y</label><input id="${prefix}_y" inputmode="decimal" value="0"></div>
    <div><label for="${prefix}_z">Z</label><input id="${prefix}_z" inputmode="decimal" value="0"></div>
    <div><label for="${prefix}_heading">Heading</label><input id="${prefix}_heading" inputmode="decimal" value="0"></div>
  </div>`;
}

function renderBuilders() {
  const zoneId = state.zone ? state.zone.id : 0;
  $('builders').innerHTML = `
    <div class="box">
      <h2>Model Search And Preview-To-Real</h2>
      <div class="row"><div><label for="modelSearch">Model or NPC search</label><input id="modelSearch" placeholder="gnoll, orc, 1234"></div><button id="searchModels">Search</button></div>
      <div id="modelResults" class="status"></div>
    </div>
    <div class="box">
      <h2>NPC Or Encounter Builder</h2>
      <input type="hidden" id="npc_zone_id" value="${esc(zoneId)}">
      <div class="row"><div><label for="npc_name">Name</label><input id="npc_name" value="a new creature"></div><div><label for="npc_model_type">Model Type</label><input id="npc_model_type" inputmode="numeric" value="0"></div><div><label for="npc_level">Level</label><input id="npc_level" inputmode="numeric" value="1"></div><div><label for="npc_size">Size</label><input id="npc_size" inputmode="numeric" value="32"></div></div>
      ${positionFields('npc')}
      <div class="row"><div><label for="npc_count">Placement Count</label><input id="npc_count" inputmode="numeric" value="1"></div><div><label for="npc_radius">Cluster Radius</label><input id="npc_radius" inputmode="decimal" value="4"></div><div><label for="npc_respawn">Respawn Seconds</label><input id="npc_respawn" inputmode="numeric" value="300"></div><div><label for="npc_faction_id">Faction ID</label><input id="npc_faction_id" inputmode="numeric" value="0"></div></div>
      <div class="row"><label><input id="npc_attackable" type="checkbox" checked style="width:auto"> Attackable</label><div><label for="npc_loot_table_id">Loot Table ID</label><input id="npc_loot_table_id" inputmode="numeric" value="0"></div></div>
      <div class="toolbar"><button data-apply="npc" data-dry="true">Preview Plan</button><button class="warning" data-apply="npc" data-dry="false">Apply NPC</button></div>
    </div>
    <div class="box">
      <h2>Harvestable Node Builder</h2>
      <input type="hidden" id="harvest_zone_id" value="${esc(zoneId)}">
      <div class="row"><div><label for="harvest_name">Name</label><input id="harvest_name" value="resource node"></div><div><label for="harvest_collection_skill">Skill</label><select id="harvest_collection_skill"><option>Gathering</option><option>Mining</option><option>Foresting</option><option>Fishing</option><option>Trapping</option><option>Collecting</option></select></div><div><label for="harvest_min_skill_level">Min Skill</label><input id="harvest_min_skill_level" inputmode="numeric" value="1"></div></div>
      ${positionFields('harvest')}
      <div class="row"><div><label for="harvest_count">Node Count</label><input id="harvest_count" inputmode="numeric" value="6"></div><div><label for="harvest_radius">Cluster Radius</label><input id="harvest_radius" inputmode="decimal" value="12"></div><div><label for="harvest_respawn">Respawn Seconds</label><input id="harvest_respawn" inputmode="numeric" value="180"></div></div>
      <label for="harvest_item_ids">Harvest Item IDs</label><input id="harvest_item_ids" placeholder="1001, 1002, 1003">
      <div class="toolbar"><button data-apply="harvest" data-dry="true">Preview Plan</button><button class="warning" data-apply="harvest" data-dry="false">Apply Harvest Nodes</button></div>
    </div>
    <div class="box">
      <h2>Point Of Interest Builder</h2>
      <input type="hidden" id="poi_zone_id" value="${esc(zoneId)}">
      <label for="poi_name">Name</label><input id="poi_name" value="New Discovery">
      ${positionFields('poi')}
      <div class="row"><div><label for="poi_grid_id">Grid ID</label><input id="poi_grid_id" inputmode="numeric" value="0"></div><label><input id="poi_discovery" type="checkbox" checked style="width:auto"> Discovery</label><label><input id="poi_include_y" type="checkbox" style="width:auto"> Include Y</label></div>
      <div class="toolbar"><button data-apply="poi" data-dry="true">Preview Plan</button><button class="warning" data-apply="poi" data-dry="false">Apply POI</button></div>
    </div>
    <div class="box">
      <h2>Item And Loot Builder</h2>
      <div class="row"><div><label for="loot_item_name">Item Name</label><input id="loot_item_name" value="New Item"></div><div><label for="loot_item_type">Item Type</label><select id="loot_item_type"><option>Normal</option><option>Weapon</option><option>Armor</option><option>Shield</option><option>Food</option><option>Bauble</option><option>House</option></select></div><div><label for="loot_level">Level</label><input id="loot_level" inputmode="numeric" value="1"></div><div><label for="loot_tier">Tier</label><input id="loot_tier" inputmode="numeric" value="1"></div></div>
      <label for="loot_description">Description</label><textarea id="loot_description"></textarea>
      <div class="row"><label><input id="loot_create_item" type="checkbox" checked style="width:auto"> Create item</label><div><label for="loot_item_id">Existing Item ID</label><input id="loot_item_id" inputmode="numeric" value="0"></div><div><label for="loot_loottable_id">Existing Loot Table ID</label><input id="loot_loottable_id" inputmode="numeric" value="0"></div><div><label for="loot_spawn_id">Attach To Spawn ID</label><input id="loot_spawn_id" inputmode="numeric" value="0"></div></div>
      <div class="row"><div><label for="loot_loottable_name">Loot Table Name</label><input id="loot_loottable_name" value="New loot table"></div><div><label for="loot_drop_probability">Drop Probability</label><input id="loot_drop_probability" inputmode="decimal" value="25"></div><div><label for="loot_icon">Icon</label><input id="loot_icon" inputmode="numeric" value="0"></div></div>
      <div class="toolbar"><button data-apply="loot" data-dry="true">Preview Plan</button><button class="warning" data-apply="loot" data-dry="false">Apply Item And Loot</button></div>
    </div>
    <div class="box">
      <h2>Dialogue And Hail Editor</h2>
      <div class="row"><div><label for="dialog_spawn_id">Spawn ID</label><input id="dialog_spawn_id" inputmode="numeric" value="0"></div><div><label for="dialog_npc_name">NPC Name</label><input id="dialog_npc_name" value="npc"></div><div><label for="dialog_quest_id">Quest ID To Offer</label><input id="dialog_quest_id" inputmode="numeric" value="0"></div></div>
      <label for="dialog_hail_text">Hail Text</label><textarea id="dialog_hail_text">Hello there.</textarea>
      <div class="row"><div><label for="dialog_quest_option">Quest Option Text</label><input id="dialog_quest_option" value="I can help."></div><div><label for="dialog_close_option">Close Option Text</label><input id="dialog_close_option" value="Maybe later."></div></div>
      <div class="row"><label><input id="dialog_write_file" type="checkbox" checked style="width:auto"> Write Lua file</label><label><input id="dialog_attach_script" type="checkbox" checked style="width:auto"> Attach spawn script</label></div>
      <div class="toolbar"><button data-apply="dialog" data-dry="true">Preview Lua</button><button class="warning" data-apply="dialog" data-dry="false">Apply Dialogue</button></div>
    </div>
    <div id="applyResult" class="box" aria-live="polite"><h2>Plan And Result</h2><pre id="applyOutput">No action yet.</pre></div>`;

  document.querySelectorAll('[data-apply]').forEach(button => button.addEventListener('click', () => applyAction(button.dataset.apply, button.dataset.dry === 'true')));
  $('searchModels').addEventListener('click', searchModels);
}

function collect(prefix, fields) {
  const body = {};
  for (const field of fields) {
    const el = $(`${prefix}_${field}`);
    if (!el) continue;
    body[field] = el.type === 'checkbox' ? el.checked : el.value;
  }
  return body;
}

function actionBody(action, dryRun) {
  const common = { action, dry_run: dryRun };
  if (action === 'npc') return { ...common, ...collect('npc', ['zone_id','name','model_type','level','size','x','y','z','heading','count','radius','respawn','faction_id','attackable','loot_table_id']) };
  if (action === 'harvest') return { ...common, ...collect('harvest', ['zone_id','name','collection_skill','min_skill_level','x','y','z','heading','count','radius','respawn','item_ids']) };
  if (action === 'poi') return { ...common, ...collect('poi', ['zone_id','name','x','y','z','heading','grid_id','discovery','include_y']) };
  if (action === 'loot') return { ...common, ...collect('loot', ['item_name','item_type','description','item_id','tier','level','icon','spawn_id','loottable_id','loottable_name','drop_probability','create_item']) };
  if (action === 'dialog') return { ...common, ...collect('dialog', ['spawn_id','npc_name','quest_id','hail_text','quest_option','close_option','write_file','attach_script']) };
  return common;
}

async function applyAction(action, dryRun) {
  $('applyOutput').textContent = 'Working...';
  try {
    const result = await postJson('/content/api/apply', actionBody(action, dryRun));
    $('applyOutput').textContent = JSON.stringify(result, null, 2);
    if (!dryRun && result.success == 1) await loadZone();
  } catch (err) {
    $('applyOutput').textContent = String(err);
  }
}

async function searchModels() {
  const search = $('modelSearch').value;
  $('modelResults').textContent = 'Searching...';
  const data = await postJson('/content/api/search', { kind: 'models', search });
  const models = data.models || [];
  const spawns = data.npc_models || [];
  $('modelResults').innerHTML = `
    <h3>Model Metadata</h3>
    ${table(['Model', 'Category', 'Subcategory', 'Name', 'Use'], models.map(m => `<tr><td>${esc(m.model_type)}</td><td>${esc(m.category)}</td><td>${esc(m.subcategory)}</td><td>${esc(m.model_name)}</td><td><button class="secondary" data-use-model="${esc(m.model_type)}">Use</button></td></tr>`))}
    <h3>Existing NPC Models</h3>
    ${table(['Spawn', 'Name', 'Model', 'Level', 'Use'], spawns.map(s => `<tr><td>${esc(s.spawn_id)}</td><td>${esc(s.name)}</td><td>${esc(s.model_type)}</td><td>${esc(s.min_level)}-${esc(s.max_level)}</td><td><button class="secondary" data-use-model="${esc(s.model_type)}" data-use-size="${esc(s.size)}" data-use-level="${esc(s.min_level)}">Use</button></td></tr>`))}`;
  $('modelResults').querySelectorAll('[data-use-model]').forEach(button => button.addEventListener('click', () => {
    $('npc_model_type').value = button.dataset.useModel;
    if (button.dataset.useSize) $('npc_size').value = button.dataset.useSize;
    if (button.dataset.useLevel) $('npc_level').value = button.dataset.useLevel;
    $('npc_name').focus();
  }));
}

function useEditorPosition() {
  const name = $('editorSelect').value;
  const editor = state.editors.find(e => e.character_name === name);
  if (!editor) return;
  const prefixes = ['npc', 'harvest', 'poi'];
  for (const prefix of prefixes) {
    if ($(`${prefix}_zone_id`)) $(`${prefix}_zone_id`).value = editor.zone_id;
    if ($(`${prefix}_x`)) $(`${prefix}_x`).value = fmt(editor.x);
    if ($(`${prefix}_y`)) $(`${prefix}_y`).value = fmt(editor.y);
    if ($(`${prefix}_z`)) $(`${prefix}_z`).value = fmt(editor.z);
    if ($(`${prefix}_heading`)) $(`${prefix}_heading`).value = fmt(editor.heading);
  }
  const zoneOption = Array.from($('zoneSelect').options).find(o => Number(o.value) === Number(editor.zone_id));
  if (zoneOption) $('zoneSelect').value = zoneOption.value;
  setStatus(`Copied ${editor.character_name}'s position into the builder forms.`);
}

document.querySelectorAll('.tab').forEach(tab => tab.addEventListener('click', () => {
  document.querySelectorAll('.tab').forEach(t => t.setAttribute('aria-selected', 'false'));
  document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
  tab.setAttribute('aria-selected', 'true');
  $(tab.getAttribute('aria-controls')).classList.add('active');
}));
$('loadZone').addEventListener('click', loadZone);
$('refreshAll').addEventListener('click', async () => { await bootstrap(); if (state.zone) await loadZone(); });
$('useEditor').addEventListener('click', useEditorPosition);
bootstrap().catch(err => setStatus(String(err)));
</script>
</body>
</html>)HTML";
}
}

using boost::property_tree::ptree;

void World::Web_worldhandle_dashboard(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
	res.set(http::field::content_type, kHtmlType);
	res.body() = DashboardHtml();
	res.prepare_payload();
}

void World::Web_worldhandle_routes(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
	ptree response;
	PopulateRouteMetadata(response);
	response.put("success", 1);
	WriteJson(res, response);
}

void ZoneList::PopulateContentEditorList(boost::property_tree::ptree& pt) {
	ptree editors;

	MClientList.lock();
	for (auto& itr : client_map) {
		Client* client = itr.second;
		if (!client || !client->GetPlayer())
			continue;

		Player* player = client->GetPlayer();
		ptree editor;
		editor.put("character_id", client->GetCharacterID());
		editor.put("character_name", player->GetName());
		editor.put("zone_id", player->GetZone() ? player->GetZone()->GetZoneID() : 0);
		editor.put("zone_name", player->GetZone() ? player->GetZone()->GetZoneName() : "");
		editor.put("x", player->GetX());
		editor.put("y", player->GetY());
		editor.put("z", player->GetZ());
		editor.put("heading", player->GetHeading());
		editors.push_back(std::make_pair("", editor));
	}
	MClientList.unlock();

	pt.add_child("editors", editors);
}

void World::Web_worldhandle_content(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
	res.set(http::field::content_type, kHtmlType);
	res.body() = ContentWorkbenchHtml();
	res.prepare_payload();
}

void World::Web_worldhandle_content_bootstrap(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
	ptree response;
	PopulateZones(response);
	zone_list.PopulateContentEditorList(response);
	response.put("success", 1);
	WriteJson(res, response);
}

void World::Web_worldhandle_content_zone(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
	ptree body, response;
	if (!ReadJsonBody(req, body, response)) {
		WriteJson(res, response);
		return;
	}

	int32 zone_id = GetInt(body, "zone_id", 0);
	std::string zone_name;
	if (zone_id <= 0 || !PopulateZoneHeader(zone_id, response, zone_name)) {
		response.put("success", 0);
		response.put("error", "Zone not found.");
		WriteJson(res, response);
		return;
	}

	PopulateZoneSummary(zone_id, zone_name, response);
	PopulateNpcList(zone_id, response);
	PopulateGroundspawnList(zone_id, response);
	PopulateQuestList(zone_id, zone_name, response);
	PopulatePoiList(zone_id, response);
	PopulateMapPoints(zone_id, response);
	PopulateAudit(zone_id, zone_name, response);
	PopulateQuestValidation(zone_id, zone_name, response);
	response.put("success", 1);
	WriteJson(res, response);
}

void World::Web_worldhandle_content_search(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
	ptree body, response;
	if (!ReadJsonBody(req, body, response)) {
		WriteJson(res, response);
		return;
	}

	std::string search = GetString(body, "search", "");
	ptree models;
	for (auto& model : database.GetModelViewerModels(search, 50)) {
		ptree item;
		item.put("model_type", model.model_type);
		item.put("category", model.category);
		item.put("subcategory", model.subcategory);
		item.put("model_name", model.model_name);
		models.push_back(std::make_pair("", item));
	}
	response.add_child("models", models);

	ptree npc_models;
	for (auto& spawn : database.GetModelViewerSpawns(search, 50)) {
		ptree item;
		item.put("spawn_id", spawn.spawn_id);
		item.put("name", spawn.name);
		item.put("model_type", spawn.model_type);
		item.put("soga_model_type", spawn.soga_model_type);
		item.put("min_level", spawn.min_level);
		item.put("max_level", spawn.max_level);
		item.put("size", spawn.size);
		item.put("heroic_flag", spawn.heroic_flag);
		npc_models.push_back(std::make_pair("", item));
	}
	response.add_child("npc_models", npc_models);
	response.put("success", 1);
	WriteJson(res, response);
}

void World::Web_worldhandle_content_apply(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
	ptree body, response;
	if (!ReadJsonBody(req, body, response)) {
		WriteJson(res, response);
		return;
	}

	std::string action = GetString(body, "action", "");
	if (action == "npc")
		ApplyNpc(body, response);
	else if (action == "harvest")
		ApplyHarvest(body, response);
	else if (action == "poi")
		ApplyPoi(body, response);
	else if (action == "loot")
		ApplyLoot(body, response);
	else if (action == "dialog")
		ApplyDialog(body, response);
	else {
		response.put("success", 0);
		response.put("error", "Unknown content action.");
	}

	WriteJson(res, response);
}
