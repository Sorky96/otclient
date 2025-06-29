// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "npc.h"

#include "game.h"
#include "pugicast.h"
#include "spectators.h"

extern Game g_game;
extern LuaEnvironment g_luaEnvironment;

uint32_t Npc::npcAutoID = 0x20000000;

namespace Npcs {
bool loaded = false;
std::shared_ptr<NpcScriptInterface> scriptInterface = std::make_shared<NpcScriptInterface>();
std::map<const std::string, NpcType*> npcTypes;

void load(bool reload /*= false*/)
{
	if (!reload && loaded) {
		return;
	}

	scriptInterface->loadFile("data/global.lua");
	if (!scriptInterface->loadNpcLib("data/npc/lib/npc.lua")) {
		std::cout << "[Warning - NpcLib::NpcLib] Can not load lib: data/npc/lib/npc.lua" << std::endl;
		std::cout << scriptInterface->getLastLuaError() << std::endl;
		return;
	}

	loaded = true;

	if (!reload) {
		fmt::print(">> NpcLib loaded\n");
	} else {
		fmt::print(">> NpcLib reloaded\n");
	}
}

void reload()
{
	load(true);

	const std::map<uint32_t, Npc*>& npcs = g_game.getNpcs();
	for (const auto& it : npcs) {
		if (it.second) {
			it.second->closeAllShopWindows();
		}
	}

	for (const auto& it : getNpcTypes()) {
		if (it.second && !it.second->fromLua) {
			it.second->loadFromXml();
		}
	}

	for (const auto& it : npcs) {
		if (it.second && !it.second->npcType->fromLua) {
			it.second->reload();
		}
	}
}

void addNpcType(const std::string& name, NpcType* npcType) { npcTypes[name] = npcType; }
void clearNpcTypes() { npcTypes.clear(); }
std::map<const std::string, NpcType*> getNpcTypes() { return npcTypes; }

NpcType* getNpcType(std::string name)
{
	auto npcType = npcTypes[name];
	if (npcType) {
		return npcType;
	}
	return nullptr;
}

NpcScriptInterface* getScriptInterface() { return scriptInterface.get(); }

bool loadNpcs(bool reload)
{
	namespace fs = std::filesystem;

	const auto dir = fs::current_path() / "data/npc/lua";
	if (!fs::exists(dir) || !fs::is_directory(dir)) {
		std::cout << "[Warning - Npcs::loadNpcs] Can not load folder 'npc/lua'" << std::endl;
		return false;
	}

	fs::recursive_directory_iterator endit;
	std::vector<fs::path> v;
	std::string disable = ("#");
	for (fs::recursive_directory_iterator it(dir); it != endit; ++it) {
		auto fn = it->path().parent_path().filename();
		if (fs::is_regular_file(*it) && it->path().extension() == ".lua") {
			size_t found = it->path().filename().string().find(disable);
			if (found != std::string::npos) {
				if (getBoolean(ConfigManager::SCRIPTS_CONSOLE_LOGS)) {
					std::cout << "> " << it->path().filename().string() << " [disabled]" << std::endl;
				}
				continue;
			}
			v.push_back(it->path());
		}
	}
	sort(v.begin(), v.end());
	std::string redir;
	for (auto it = v.begin(); it != v.end(); ++it) {
		const std::string scriptFile = it->string();
		if (redir.empty() || redir != it->parent_path().string()) {
			auto p = fs::path(it->relative_path());
			if (getBoolean(ConfigManager::SCRIPTS_CONSOLE_LOGS)) {
				std::cout << ">> [" << p.parent_path().filename() << "]" << std::endl;
			}
			redir = it->parent_path().string();
		}

		if (scriptInterface->loadFile(scriptFile) == -1) {
			std::cout << "> " << it->filename().string() << " [error]" << std::endl;
			std::cout << "^ " << scriptInterface->getLastLuaError() << std::endl;
			continue;
		}

		if (getBoolean(ConfigManager::SCRIPTS_CONSOLE_LOGS)) {
			if (!reload) {
				std::cout << "> " << it->filename().string() << " [loaded]" << std::endl;
			} else {
				std::cout << "> " << it->filename().string() << " [reloaded]" << std::endl;
			}
		}
	}

	return true;
}
} // namespace Npcs

int32_t NpcScriptInterface::loadFile(const std::string& file, Npc* npc /* = nullptr*/)
{
	// loads file as a chunk at stack top
	int ret = luaL_loadfile(L, file.data());
	if (ret != 0) {
		lastLuaError = tfs::lua::popString(L);
		return -1;
	}

	// check that it is loaded as a function
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return -1;
	}

	loadingFile = file;

	if (!tfs::lua::reserveScriptEnv()) {
		lua_pop(L, 1);
		return -1;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(Npcs::EVENT_ID_LOADING, this);
	env->setNpc(npc);

	// execute it
	ret = tfs::lua::protectedCall(L, 0, 0);
	if (ret != 0) {
		reportErrorFunc(nullptr, tfs::lua::popString(L));
		tfs::lua::resetScriptEnv();
		return -1;
	}

	tfs::lua::resetScriptEnv();
	return 0;
}

Npc* Npc::createNpc(const std::string& name)
{
	auto npcType = Npcs::getNpcType(name);
	if (!npcType) {
		npcType = new NpcType();
		npcType->filename = "data/npc/" + name + ".xml";
		if (!npcType->loadFromXml()) {
			delete npcType;
			return nullptr;
		}
	}
	Npc* npc = new Npc(name);
	npc->setName(name);
	npc->loaded = true;
	npc->npcType = npcType;
	npc->loadNpcTypeInfo();
	if (npcType->fromLua) {
		npc->npcEventHandler = std::make_unique<NpcEventsHandler>(*npcType->npcEventHandler);
	} else {
		npc->npcEventHandler = std::make_unique<NpcEventsHandler>(npcType->scriptFilename, npc);
	}
	npc->npcEventHandler->setNpc(npc);
	return npc;
}

Npc::Npc(const std::string& name) : Creature(), filename("data/npc/" + name + ".xml"), masterRadius(-1), loaded(false)
{
	reset();
}

Npc::~Npc() { reset(); }

void Npc::addList() { g_game.addNpc(this); }

void Npc::removeList() { g_game.removeNpc(this); }

bool Npc::load()
{
	if (loaded) {
		return true;
	}

	loadNpcTypeInfo();
	npcEventHandler = std::make_unique<NpcEventsHandler>(npcType->scriptFilename, this);
	npcEventHandler->setNpc(this);

	loaded = true;
	return loaded;
}

void Npc::reset(bool reload)
{
	loaded = false;
	isIdle = true;
	walkTicks = 1500;
	pushable = true;
	floorChange = false;
	attackable = false;
	ignoreHeight = false;
	focusCreature = 0;
	speechBubble = SPEECHBUBBLE_NONE;

	npcEventHandler.reset();

	parameters.clear();
	shopPlayerSet.clear();
	spectators.clear();

	if (reload) {
		load();
	}
}

void Npc::reload()
{
	if (!npcType->fromLua) {
		reset(true);
	}

	SpectatorVec players;
	g_game.map.getSpectators(players, getPosition(), true, true);
	for (const auto& player : players) {
		assert(dynamic_cast<Player*>(player) != nullptr);
		spectators.insert(static_cast<Player*>(player));
	}

	const bool hasSpectators = !spectators.empty();
	setIdle(!hasSpectators);

	if (hasSpectators && walkTicks > 0) {
		addEventWalk();
	}

	// Simulate that the creature is placed on the map again.
	if (npcEventHandler) {
		npcEventHandler->onCreatureAppear(this);
	}
}

bool NpcType::loadFromXml()
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		printXMLError("Error - Npc::loadFromXml", filename, result);
		return false;
	}

	pugi::xml_node npcNode = doc.child("npc");
	if (!npcNode) {
		std::cout << "[Error - Npc::loadFromXml] Missing npc tag in " << filename << std::endl;
		return false;
	}

	name = npcNode.attribute("name").as_string();
	attackable = npcNode.attribute("attackable").as_bool();
	floorChange = npcNode.attribute("floorchange").as_bool();

	pugi::xml_attribute attr;
	if ((attr = npcNode.attribute("speed"))) {
		baseSpeed = pugi::cast<uint32_t>(attr.value());
	} else {
		baseSpeed = 100;
	}

	if ((attr = npcNode.attribute("pushable"))) {
		pushable = attr.as_bool();
	}

	if ((attr = npcNode.attribute("walkinterval"))) {
		walkTicks = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = npcNode.attribute("walkradius"))) {
		masterRadius = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = npcNode.attribute("ignoreheight"))) {
		ignoreHeight = attr.as_bool();
	}

	if ((attr = npcNode.attribute("speechbubble"))) {
		speechBubble = pugi::cast<uint32_t>(attr.value());
	}

	if ((attr = npcNode.attribute("skull"))) {
		skull = getSkullType(boost::algorithm::to_lower_copy<std::string>(attr.as_string()));
	}

	pugi::xml_node healthNode = npcNode.child("health");
	if (healthNode) {
		if ((attr = healthNode.attribute("now"))) {
			health = pugi::cast<uint64_t>(attr.value());
		} else {
			health = 100;
		}

		if ((attr = healthNode.attribute("max"))) {
			healthMax = pugi::cast<uint64_t>(attr.value());
		} else {
			healthMax = 100;
		}

		if (health > healthMax) {
			health = healthMax;
			std::cout << "[Warning - Npc::loadFromXml] Health now is greater than health max in " << filename
			          << std::endl;
		}
	}

	pugi::xml_node lookNode = npcNode.child("look");
	if (lookNode) {
		pugi::xml_attribute lookTypeAttribute = lookNode.attribute("type");
		if (lookTypeAttribute) {
			defaultOutfit.lookType = pugi::cast<uint16_t>(lookTypeAttribute.value());
			defaultOutfit.lookHead = pugi::cast<uint16_t>(lookNode.attribute("head").value());
			defaultOutfit.lookBody = pugi::cast<uint16_t>(lookNode.attribute("body").value());
			defaultOutfit.lookLegs = pugi::cast<uint16_t>(lookNode.attribute("legs").value());
			defaultOutfit.lookFeet = pugi::cast<uint16_t>(lookNode.attribute("feet").value());
			defaultOutfit.lookAddons = pugi::cast<uint16_t>(lookNode.attribute("addons").value());
		} else if ((attr = lookNode.attribute("typeex"))) {
			defaultOutfit.lookTypeEx = pugi::cast<uint16_t>(attr.value());
		}
	}

	for (auto parameterNode : npcNode.child("parameters").children()) {
		parameters[parameterNode.attribute("key").as_string()] = parameterNode.attribute("value").as_string();
	}

	pugi::xml_attribute scriptFile = npcNode.attribute("script");
	if (scriptFile) {
		scriptFilename = scriptFile.as_string();
	}
	Npcs::addNpcType(name, this);
	return true;
}

bool Npc::canSee(const Position& pos) const
{
	if (pos.z != getPosition().z) {
		return false;
	}
	return Creature::canSee(getPosition(), pos, 3, 3);
}

std::string Npc::getDescription(int32_t) const
{
	std::string descr;
	descr.reserve(name.length() + 1);
	descr.assign(name);
	descr.push_back('.');
	return descr;
}

void Npc::loadNpcTypeInfo()
{
	speechBubble = npcType->speechBubble;
	walkTicks = npcType->walkTicks;
	baseSpeed = npcType->baseSpeed;
	masterRadius = npcType->masterRadius;
	floorChange = npcType->floorChange;
	attackable = npcType->attackable;
	ignoreHeight = npcType->ignoreHeight;
	isIdle = npcType->isIdle;
	pushable = npcType->pushable;
	defaultOutfit = npcType->defaultOutfit;
	currentOutfit = defaultOutfit;
	parameters = npcType->parameters;
	health = npcType->health;
	healthMax = npcType->healthMax;
	sightX = npcType->sightX;
	sightY = npcType->sightY;
}

void Npc::onCreatureAppear(Creature* creature, bool isLogin)
{
	Creature::onCreatureAppear(creature, isLogin);

	if (creature == this) {
		if (walkTicks > 0) {
			addEventWalk();
		}

		if (npcEventHandler) {
			npcEventHandler->onCreatureAppear(creature);
		}
	} else if (creature->getPlayer()) {
		if (npcEventHandler) {
			npcEventHandler->onCreatureAppear(creature);
		}
	}
}

bool NpcType::loadCallback(NpcScriptInterface* scriptInterface)
{
	int32_t id = scriptInterface->getEvent();
	if (id == -1) {
		std::cout << "[Warning - Npc::loadCallback] Event not found. " << std::endl;
		return false;
	}

	if (eventType == "say") {
		npcEventHandler->creatureSayEvent = id;
	} else if (eventType == "disappear") {
		npcEventHandler->creatureDisappearEvent = id;
	} else if (eventType == "appear") {
		npcEventHandler->creatureAppearEvent = id;
	} else if (eventType == "move") {
		npcEventHandler->creatureMoveEvent = id;
	} else if (eventType == "closechannel") {
		npcEventHandler->playerCloseChannelEvent = id;
	} else if (eventType == "endtrade") {
		npcEventHandler->playerEndTradeEvent = id;
	} else if (eventType == "think") {
		npcEventHandler->thinkEvent = id;
	} else if (eventType == "sight") {
		npcEventHandler->creatureSightEvent = id;
	} else if (eventType == "speechbubble") {
		npcEventHandler->speechBubbleEvent = id;
	}
	return true;
}

void Npc::onRemoveCreature(Creature* creature, bool isLogout)
{
	Creature::onRemoveCreature(creature, isLogout);

	if (creature == this) {
		closeAllShopWindows();
		if (npcEventHandler) {
			npcEventHandler->onCreatureDisappear(creature);
		}
	} else if (creature->getPlayer()) {
		if (npcEventHandler) {
			npcEventHandler->onCreatureDisappear(creature);
		}
	}
}

void Npc::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
                         const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	if (creature == this || creature->getPlayer()) {
		if (npcEventHandler) {
			npcEventHandler->onCreatureMove(creature, oldPos, newPos);
		}
	}
}

void Npc::onCreatureSay(Creature* creature, SpeakClasses type, const std::string& text)
{
	if (creature == this) {
		return;
	}

	if (npcEventHandler) {
		npcEventHandler->onCreatureSay(creature, type, text);
	}
}

void Npc::onPlayerCloseChannel(Player* player)
{
	if (npcEventHandler) {
		npcEventHandler->onPlayerCloseChannel(player);
	}
}

void Npc::onThink(uint32_t interval)
{
	SpectatorVec players;
	g_game.map.getSpectators(players, getPosition(), true, true, Npcs::ViewportX, Npcs::ViewportX, Npcs::ViewportY,
	                         Npcs::ViewportY);
	for (const auto& player : players) {
		assert(dynamic_cast<Player*>(player) != nullptr);
		spectators.insert(static_cast<Player*>(player));
	}

	if (sightX > 0 || sightY > 0) {
		SpectatorVec tempCreatures;
		g_game.map.getSpectators(tempCreatures, getPosition(), false, false, Npcs::ViewportX, Npcs::ViewportX,
		                         Npcs::ViewportY, Npcs::ViewportY);
		std::erase_if(spectatorCache, [&](auto const& it) {
			return std::find(tempCreatures.begin(), tempCreatures.end(), it) == tempCreatures.end();
		});
		SpectatorVec sightCreatures;
		g_game.map.getSpectators(sightCreatures, getPosition(), false, false, sightX, sightX, sightY, sightY);
		for (const auto& creature : sightCreatures) {
			if (!spectatorCache.contains(creature)) {
				if (npcEventHandler) {
					if (this != creature) {
						npcEventHandler->onCreatureSight(creature);
					}
				}
				spectatorCache.insert(creature);
			}
		}
	}

	setIdle(spectators.empty());

	if (isIdle) {
		return;
	}

	Creature::onThink(interval);

	if (npcEventHandler) {
		npcEventHandler->onThink();
	}

	if (getTimeSinceLastMove() >= walkTicks) {
		addEventWalk();
	}

	spectators.clear();
}

void Npc::doSay(const std::string& text, SpeakClasses talkType)
{
	g_game.internalCreatureSay(this, talkType, text, false);
}

void Npc::doSayToPlayer(Player* player, const std::string& text)
{
	if (player) {
		player->sendCreatureSay(this, TALKTYPE_PRIVATE_NP, text);
		player->onCreatureSay(this, TALKTYPE_PRIVATE_NP, text);
	}
}

void Npc::onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint16_t amount,
                        bool ignore /* = false*/, bool inBackpacks /* = false*/)
{
	if (npcEventHandler) {
		npcEventHandler->onPlayerTrade(player, callback, itemId, count, amount, ignore, inBackpacks);
	}
	player->sendSaleItemList();
}

void Npc::onPlayerEndTrade(Player* player, int32_t buyCallback, int32_t sellCallback)
{
	lua_State* L = Npcs::getScriptInterface()->getLuaState();

	if (buyCallback != -1) {
		luaL_unref(L, LUA_REGISTRYINDEX, buyCallback);
	}

	if (sellCallback != -1) {
		luaL_unref(L, LUA_REGISTRYINDEX, sellCallback);
	}

	removeShopPlayer(player);

	if (npcEventHandler) {
		npcEventHandler->onPlayerEndTrade(player);
	}
}

bool Npc::getNextStep(Direction& dir, uint32_t& flags)
{
	if (Creature::getNextStep(dir, flags)) {
		return true;
	}

	if (walkTicks == 0) {
		return false;
	}

	if (focusCreature != 0) {
		return false;
	}

	if (getTimeSinceLastMove() < walkTicks) {
		return false;
	}

	return getRandomStep(dir);
}

void Npc::setIdle(const bool idle)
{
	if (idle == isIdle) {
		return;
	}

	if (isRemoved() || isDead()) {
		return;
	}

	isIdle = idle;

	if (isIdle) {
		onIdleStatus();
	}
}

bool Npc::canWalkTo(const Position& fromPos, Direction dir) const
{
	if (masterRadius == 0) {
		return false;
	}

	Position toPos = getNextPosition(dir, fromPos);
	if (!Spawns::isInZone(masterPos, masterRadius, toPos)) {
		return false;
	}

	Tile* tile = g_game.map.getTile(toPos);
	if (!tile || tile->queryAdd(0, *this, 1, 0) != RETURNVALUE_NOERROR) {
		return false;
	}

	if (!floorChange && (tile->hasFlag(TILESTATE_FLOORCHANGE) || tile->getTeleportItem())) {
		return false;
	}

	if (!ignoreHeight && tile->hasHeight(1)) {
		return false;
	}

	return true;
}

bool Npc::getRandomStep(Direction& direction) const
{
	const Position& creaturePos = getPosition();
	for (Direction dir : getShuffleDirections()) {
		if (canWalkTo(creaturePos, dir)) {
			direction = dir;
			return true;
		}
	}
	return false;
}

bool Npc::doMoveTo(const Position& pos, int32_t minTargetDist /* = 1*/, int32_t maxTargetDist /* = 1*/,
                   bool fullPathSearch /* = true*/, bool clearSight /* = true*/, int32_t maxSearchDist /* = 0*/)
{
	listWalkDir.clear();
	if (getPathTo(pos, listWalkDir, minTargetDist, maxTargetDist, fullPathSearch, clearSight, maxSearchDist)) {
		startAutoWalk();
		return true;
	}
	return false;
}

void Npc::turnToCreature(Creature* creature)
{
	const Position& creaturePos = creature->getPosition();
	const Position& myPos = getPosition();
	const auto dx = myPos.getOffsetX(creaturePos);
	const auto dy = myPos.getOffsetY(creaturePos);

	float tan;
	if (dx != 0) {
		tan = static_cast<float>(dy) / dx;
	} else {
		tan = 10;
	}

	Direction dir;
	if (std::abs(tan) < 1) {
		if (dx > 0) {
			dir = DIRECTION_WEST;
		} else {
			dir = DIRECTION_EAST;
		}
	} else {
		if (dy > 0) {
			dir = DIRECTION_NORTH;
		} else {
			dir = DIRECTION_SOUTH;
		}
	}
	g_game.internalCreatureTurn(this, dir);
}

void Npc::setCreatureFocus(Creature* creature)
{
	if (creature) {
		focusCreature = creature->getID();
		turnToCreature(creature);
	} else {
		focusCreature = 0;
	}
}

void Npc::addShopPlayer(Player* player) { shopPlayerSet.insert(player); }

void Npc::removeShopPlayer(Player* player) { shopPlayerSet.erase(player); }

void Npc::closeAllShopWindows()
{
	while (!shopPlayerSet.empty()) {
		Player* player = *shopPlayerSet.begin();
		if (!player->closeShopWindow()) {
			removeShopPlayer(player);
		}
	}
}

NpcScriptInterface::NpcScriptInterface() : LuaScriptInterface("Npc interface")
{
	libLoaded = false;
	initState();
}

bool NpcScriptInterface::initState()
{
	L = g_luaEnvironment.getLuaState();
	if (!L) {
		return false;
	}

	registerFunctions();

	lua_newtable(L);
	eventTableRef = luaL_ref(L, LUA_REGISTRYINDEX);
	runningEventId = EVENT_ID_USER;
	return true;
}

bool NpcScriptInterface::closeState()
{
	libLoaded = false;
	LuaScriptInterface::closeState();
	return true;
}

bool NpcScriptInterface::loadNpcLib(const std::string& file)
{
	if (libLoaded) {
		return true;
	}

	if (loadFile(file) == -1) {
		std::cout << "[Warning - NpcScriptInterface::loadNpcLib] Can not load " << file << std::endl;
		return false;
	}

	libLoaded = true;
	return true;
}

void NpcScriptInterface::registerFunctions()
{
	// npc exclusive functions
	lua_register(L, "selfSay", NpcScriptInterface::luaActionSay);
	lua_register(L, "selfMove", NpcScriptInterface::luaActionMove);
	lua_register(L, "selfMoveTo", NpcScriptInterface::luaActionMoveTo);
	lua_register(L, "selfTurn", NpcScriptInterface::luaActionTurn);
	lua_register(L, "selfFollow", NpcScriptInterface::luaActionFollow);
	lua_register(L, "getDistanceTo", NpcScriptInterface::luagetDistanceTo);
	lua_register(L, "doNpcSetCreatureFocus", NpcScriptInterface::luaSetNpcFocus);
	lua_register(L, "getNpcCid", NpcScriptInterface::luaGetNpcCid);
	lua_register(L, "getNpcParameter", NpcScriptInterface::luaGetNpcParameter);
	lua_register(L, "openShopWindow", NpcScriptInterface::luaOpenShopWindow);
	lua_register(L, "closeShopWindow", NpcScriptInterface::luaCloseShopWindow);
	lua_register(L, "doSellItem", NpcScriptInterface::luaDoSellItem);

	// metatable
	tfs::lua::registerMethod(L, "Npc", "getParameter", NpcScriptInterface::luaNpcGetParameter);
	tfs::lua::registerMethod(L, "Npc", "setFocus", NpcScriptInterface::luaNpcSetFocus);

	tfs::lua::registerMethod(L, "Npc", "openShopWindow", NpcScriptInterface::luaNpcOpenShopWindow);
	tfs::lua::registerMethod(L, "Npc", "closeShopWindow", NpcScriptInterface::luaNpcCloseShopWindow);
}

int NpcScriptInterface::luaActionSay(lua_State* L)
{
	// selfSay(words[, target[, talkType = TALKTYPE_SAY])
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (!npc) {
		return 0;
	}

	const std::string& text = tfs::lua::getString(L, 1);
	if (lua_gettop(L) >= 2) {
		Player* target = tfs::lua::getPlayer(L, 2);
		if (target) {
			npc->doSayToPlayer(target, text);
			return 0;
		}
	}

	SpeakClasses talkType = tfs::lua::getNumber<SpeakClasses>(L, 3, TALKTYPE_SAY);
	npc->doSay(text, talkType);
	return 0;
}

int NpcScriptInterface::luaActionMove(lua_State* L)
{
	// selfMove(direction)
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (npc) {
		g_game.internalMoveCreature(npc, tfs::lua::getNumber<Direction>(L, 1));
	}
	return 0;
}

int NpcScriptInterface::luaActionMoveTo(lua_State* L)
{
	// selfMoveTo(x, y, z[, minTargetDist = 1[, maxTargetDist = 1[, fullPathSearch = true[, clearSight = true[,
	// maxSearchDist = 0]]]]]) selfMoveTo(position[, minTargetDist = 1[, maxTargetDist = 1[, fullPathSearch = true[,
	// clearSight = true[, maxSearchDist = 0]]]]])
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (!npc) {
		return 0;
	}

	Position position;
	int32_t argsStart = 2;
	if (lua_istable(L, 1)) {
		position = tfs::lua::getPosition(L, 1);
	} else {
		position.x = tfs::lua::getNumber<uint16_t>(L, 1);
		position.y = tfs::lua::getNumber<uint16_t>(L, 2);
		position.z = tfs::lua::getNumber<uint8_t>(L, 3);
		argsStart = 4;
	}

	tfs::lua::pushBoolean(
	    L,
	    npc->doMoveTo(position, tfs::lua::getNumber<int32_t>(L, argsStart, 1),
	                  tfs::lua::getNumber<int32_t>(L, argsStart + 1, 1), tfs::lua::getBoolean(L, argsStart + 2, true),
	                  tfs::lua::getBoolean(L, argsStart + 3, true), tfs::lua::getNumber<int32_t>(L, argsStart + 4, 0)));
	return 1;
}

int NpcScriptInterface::luaActionTurn(lua_State* L)
{
	// selfTurn(direction)
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (npc) {
		g_game.internalCreatureTurn(npc, tfs::lua::getNumber<Direction>(L, 1));
	}
	return 0;
}

int NpcScriptInterface::luaActionFollow(lua_State* L)
{
	// selfFollow(player)
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (!npc) {
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	tfs::lua::pushBoolean(L, npc->setFollowCreature(tfs::lua::getPlayer(L, 1)));
	return 1;
}

int NpcScriptInterface::luagetDistanceTo(lua_State* L)
{
	// getDistanceTo(uid)
	ScriptEnvironment* env = tfs::lua::getScriptEnv();

	Npc* npc = env->getNpc();
	if (!npc) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_THING_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	uint32_t uid = tfs::lua::getNumber<uint32_t>(L, -1);

	Thing* thing = env->getThingByUID(uid);
	if (!thing) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_THING_NOT_FOUND));
		lua_pushnil(L);
		return 1;
	}

	const Position& thingPos = thing->getPosition();
	const Position& npcPos = npc->getPosition();
	if (npcPos.z != thingPos.z) {
		lua_pushnumber(L, -1);
	} else {
		lua_pushnumber(L, std::max(npcPos.getDistanceX(thingPos), npcPos.getDistanceY(thingPos)));
	}
	return 1;
}

int NpcScriptInterface::luaSetNpcFocus(lua_State* L)
{
	// doNpcSetCreatureFocus(cid)
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (npc) {
		npc->setCreatureFocus(tfs::lua::getCreature(L, -1));
	}
	return 0;
}

int NpcScriptInterface::luaGetNpcCid(lua_State* L)
{
	// getNpcCid()
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (npc) {
		lua_pushnumber(L, npc->getID());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaGetNpcParameter(lua_State* L)
{
	// getNpcParameter(paramKey)
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (!npc) {
		lua_pushnil(L);
		return 1;
	}

	std::string paramKey = tfs::lua::getString(L, -1);

	auto it = npc->parameters.find(paramKey);
	if (it != npc->parameters.end()) {
		tfs::lua::pushString(L, it->second);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaOpenShopWindow(lua_State* L)
{
	// openShopWindow(cid, items, onBuy callback, onSell callback)
	int32_t sellCallback;
	if (!lua_isfunction(L, -1)) {
		sellCallback = -1;
		lua_pop(L, 1); // skip it - use default value
	} else {
		sellCallback = tfs::lua::popCallback(L);
	}

	int32_t buyCallback;
	if (!lua_isfunction(L, -1)) {
		buyCallback = -1;
		lua_pop(L, 1); // skip it - use default value
	} else {
		buyCallback = tfs::lua::popCallback(L);
	}

	if (!lua_istable(L, -1)) {
		reportErrorFunc(L, "item list is not a table.");
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	std::list<ShopInfo> items;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		const auto tableIndex = lua_gettop(L);
		ShopInfo item;

		item.itemId = tfs::lua::getField<uint32_t>(L, tableIndex, "id");
		item.subType = tfs::lua::getField<int32_t>(L, tableIndex, "subType");
		if (item.subType == 0) {
			item.subType = tfs::lua::getField<int32_t>(L, tableIndex, "subtype");
			lua_pop(L, 1);
		}

		item.buyPrice = tfs::lua::getField<int64_t>(L, tableIndex, "buy");
		item.sellPrice = tfs::lua::getField<int64_t>(L, tableIndex, "sell");
		item.realName = tfs::lua::getFieldString(L, tableIndex, "name");

		items.push_back(item);
		lua_pop(L, 6);
	}
	lua_pop(L, 1);

	Player* player = tfs::lua::getPlayer(L, -1);
	if (!player) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_PLAYER_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	// Close any eventual other shop window currently open.
	player->closeShopWindow(false);

	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (!npc) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_CREATURE_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	npc->addShopPlayer(player);
	player->setShopOwner(npc, buyCallback, sellCallback);
	player->openShopWindow(items);

	tfs::lua::pushBoolean(L, true);
	return 1;
}

int NpcScriptInterface::luaCloseShopWindow(lua_State* L)
{
	// closeShopWindow(cid)
	Npc* npc = tfs::lua::getScriptEnv()->getNpc();
	if (!npc) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_CREATURE_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	Player* player = tfs::lua::getPlayer(L, 1);
	if (!player) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_PLAYER_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	int32_t buyCallback;
	int32_t sellCallback;

	Npc* merchant = player->getShopOwner(buyCallback, sellCallback);

	// Check if we actually have a shop window with this player.
	if (merchant == npc) {
		player->sendCloseShop();

		if (buyCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, buyCallback);
		}

		if (sellCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, sellCallback);
		}

		player->setShopOwner(nullptr, -1, -1);
		npc->removeShopPlayer(player);
	}

	tfs::lua::pushBoolean(L, true);
	return 1;
}

int NpcScriptInterface::luaDoSellItem(lua_State* L)
{
	// doSellItem(cid, itemid, amount, <optional> subtype, <optional> actionid, <optional: default: 1> canDropOnMap)
	Player* player = tfs::lua::getPlayer(L, 1);
	if (!player) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_PLAYER_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	uint32_t sellCount = 0;

	uint32_t itemId = tfs::lua::getNumber<uint32_t>(L, 2);
	uint32_t amount = tfs::lua::getNumber<uint32_t>(L, 3);
	uint32_t subType;

	int32_t n = tfs::lua::getNumber<int32_t>(L, 4, -1);
	if (n != -1) {
		subType = n;
	} else {
		subType = 1;
	}

	uint32_t actionId = tfs::lua::getNumber<uint32_t>(L, 5, 0);
	bool canDropOnMap = tfs::lua::getBoolean(L, 6, true);

	const ItemType& it = Item::items[itemId];
	if (it.stackable) {
		while (amount > 0) {
			int32_t stackCount = std::min<int32_t>(ITEM_STACK_SIZE, amount);
			Item* item = Item::CreateItem(it.id, stackCount);
			if (item && actionId != 0) {
				item->setActionId(actionId);
			}

			if (g_game.internalPlayerAddItem(player, item, canDropOnMap) != RETURNVALUE_NOERROR) {
				delete item;
				lua_pushnumber(L, sellCount);
				return 1;
			}

			amount -= stackCount;
			sellCount += stackCount;
		}
	} else {
		for (uint32_t i = 0; i < amount; ++i) {
			Item* item = Item::CreateItem(it.id, subType);
			if (item && actionId != 0) {
				item->setActionId(actionId);
			}

			if (g_game.internalPlayerAddItem(player, item, canDropOnMap) != RETURNVALUE_NOERROR) {
				delete item;
				lua_pushnumber(L, sellCount);
				return 1;
			}

			++sellCount;
		}
	}

	lua_pushnumber(L, sellCount);
	return 1;
}

int NpcScriptInterface::luaNpcGetParameter(lua_State* L)
{
	// npc:getParameter(key)
	const std::string& key = tfs::lua::getString(L, 2);
	Npc* npc = tfs::lua::getUserdata<Npc>(L, 1);
	if (npc) {
		auto it = npc->parameters.find(key);
		if (it != npc->parameters.end()) {
			tfs::lua::pushString(L, it->second);
		} else {
			lua_pushnil(L);
		}
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaNpcSetFocus(lua_State* L)
{
	// npc:setFocus(creature)
	Creature* creature = tfs::lua::getCreature(L, 2);
	Npc* npc = tfs::lua::getUserdata<Npc>(L, 1);
	if (npc) {
		npc->setCreatureFocus(creature);
		tfs::lua::pushBoolean(L, true);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

int NpcScriptInterface::luaNpcOpenShopWindow(lua_State* L)
{
	// npc:openShopWindow(cid, items, buyCallback, sellCallback)
	if (!lua_istable(L, 3)) {
		reportErrorFunc(L, "item list is not a table.");
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	Player* player = tfs::lua::getPlayer(L, 2);
	if (!player) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_PLAYER_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	Npc* npc = tfs::lua::getUserdata<Npc>(L, 1);
	if (!npc) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_CREATURE_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	int32_t sellCallback = -1;
	if (lua_isfunction(L, 5)) {
		sellCallback = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	int32_t buyCallback = -1;
	if (lua_isfunction(L, 4)) {
		buyCallback = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	std::list<ShopInfo> items;

	lua_pushnil(L);
	while (lua_next(L, 3) != 0) {
		const auto tableIndex = lua_gettop(L);
		ShopInfo item;

		item.itemId = tfs::lua::getField<uint32_t>(L, tableIndex, "id");
		item.subType = tfs::lua::getField<int32_t>(L, tableIndex, "subType");
		if (item.subType == 0) {
			item.subType = tfs::lua::getField<int32_t>(L, tableIndex, "subtype");
			lua_pop(L, 1);
		}

		item.buyPrice = tfs::lua::getField<int64_t>(L, tableIndex, "buy");
		item.sellPrice = tfs::lua::getField<int64_t>(L, tableIndex, "sell");
		item.realName = tfs::lua::getFieldString(L, tableIndex, "name");

		items.push_back(item);
		lua_pop(L, 6);
	}
	lua_pop(L, 1);

	player->closeShopWindow(false);
	npc->addShopPlayer(player);

	player->setShopOwner(npc, buyCallback, sellCallback);
	player->openShopWindow(items);

	tfs::lua::pushBoolean(L, true);
	return 1;
}

int NpcScriptInterface::luaNpcCloseShopWindow(lua_State* L)
{
	// npc:closeShopWindow(player)
	Player* player = tfs::lua::getPlayer(L, 2);
	if (!player) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_PLAYER_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	Npc* npc = tfs::lua::getUserdata<Npc>(L, 1);
	if (!npc) {
		reportErrorFunc(L, tfs::lua::getErrorDesc(LUA_ERROR_CREATURE_NOT_FOUND));
		tfs::lua::pushBoolean(L, false);
		return 1;
	}

	int32_t buyCallback;
	int32_t sellCallback;

	Npc* merchant = player->getShopOwner(buyCallback, sellCallback);
	if (merchant == npc) {
		player->sendCloseShop();
		if (buyCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, buyCallback);
		}

		if (sellCallback != -1) {
			luaL_unref(L, LUA_REGISTRYINDEX, sellCallback);
		}

		player->setShopOwner(nullptr, -1, -1);
		npc->removeShopPlayer(player);
	}

	tfs::lua::pushBoolean(L, true);
	return 1;
}

NpcEventsHandler::NpcEventsHandler(const std::string& file, Npc* npc) : scriptInterface(Npcs::scriptInterface), npc(npc)
{
	loaded = scriptInterface->loadFile("data/npc/scripts/" + file, npc) == 0;
	if (!loaded) {
		std::cout << "[Warning - NpcScript::NpcScript] Can not load script: " << file << std::endl;
		std::cout << scriptInterface->getLastLuaError() << std::endl;
	} else {
		creatureSayEvent = scriptInterface->getEvent("onCreatureSay");
		creatureDisappearEvent = scriptInterface->getEvent("onCreatureDisappear");
		creatureAppearEvent = scriptInterface->getEvent("onCreatureAppear");
		creatureMoveEvent = scriptInterface->getEvent("onCreatureMove");
		playerCloseChannelEvent = scriptInterface->getEvent("onPlayerCloseChannel");
		playerEndTradeEvent = scriptInterface->getEvent("onPlayerEndTrade");
		thinkEvent = scriptInterface->getEvent("onThink");
	}
}

NpcEventsHandler::NpcEventsHandler() : scriptInterface(Npcs::scriptInterface) {}

NpcEventsHandler::~NpcEventsHandler()
{
	for (auto eventId :
	     {creatureSayEvent, creatureDisappearEvent, creatureAppearEvent, creatureMoveEvent, playerCloseChannelEvent,
	      playerEndTradeEvent, thinkEvent, speechBubbleEvent, creatureSightEvent}) {
		if (npc && !npc->npcType->fromLua) {
			scriptInterface->removeEvent(eventId);
		}
	}
}

bool NpcEventsHandler::isLoaded() const { return loaded; }

void NpcEventsHandler::onCreatureAppear(Creature* creature)
{
	if (creatureAppearEvent == -1) {
		return;
	}

	// onCreatureAppear(creature)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onCreatureAppear] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(creatureAppearEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureAppearEvent);
	tfs::lua::pushUserdata(L, creature);
	tfs::lua::setCreatureMetatable(L, -1, creature);
	scriptInterface->callFunction(1);
}

void NpcEventsHandler::onCreatureDisappear(Creature* creature)
{
	if (creatureDisappearEvent == -1) {
		return;
	}

	// onCreatureDisappear(creature)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onCreatureDisappear] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(creatureDisappearEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureDisappearEvent);
	tfs::lua::pushUserdata(L, creature);
	tfs::lua::setCreatureMetatable(L, -1, creature);
	scriptInterface->callFunction(1);
}

void NpcEventsHandler::onCreatureMove(Creature* creature, const Position& oldPos, const Position& newPos)
{
	if (creatureMoveEvent == -1) {
		return;
	}

	// onCreatureMove(creature, oldPos, newPos)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onCreatureMove] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(creatureMoveEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureMoveEvent);
	tfs::lua::pushUserdata(L, creature);
	tfs::lua::setCreatureMetatable(L, -1, creature);
	tfs::lua::pushPosition(L, oldPos);
	tfs::lua::pushPosition(L, newPos);
	scriptInterface->callFunction(3);
}

void NpcEventsHandler::onCreatureSay(Creature* creature, SpeakClasses type, const std::string& text)
{
	if (creatureSayEvent == -1) {
		return;
	}

	// onCreatureSay(creature, type, msg)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onCreatureSay] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(creatureSayEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureSayEvent);
	tfs::lua::pushUserdata(L, creature);
	tfs::lua::setCreatureMetatable(L, -1, creature);
	lua_pushnumber(L, type);
	tfs::lua::pushString(L, text);
	scriptInterface->callFunction(3);
}

void NpcEventsHandler::onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint16_t amount,
                                     bool ignore, bool inBackpacks)
{
	if (callback == -1) {
		return;
	}

	// onBuy(player, itemid, count, amount, ignore, inbackpacks)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onPlayerTrade] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(-1, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	tfs::lua::pushCallback(L, callback);
	tfs::lua::pushUserdata(L, player);
	tfs::lua::setMetatable(L, -1, "Player");
	lua_pushnumber(L, itemId);
	lua_pushnumber(L, count);
	lua_pushnumber(L, amount);
	tfs::lua::pushBoolean(L, ignore);
	tfs::lua::pushBoolean(L, inBackpacks);
	scriptInterface->callFunction(6);
}

void NpcEventsHandler::onPlayerCloseChannel(Player* player)
{
	if (playerCloseChannelEvent == -1) {
		return;
	}

	// onPlayerCloseChannel(player)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onPlayerCloseChannel] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(playerCloseChannelEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(playerCloseChannelEvent);
	tfs::lua::pushUserdata(L, player);
	tfs::lua::setMetatable(L, -1, "Player");
	scriptInterface->callFunction(1);
}

void NpcEventsHandler::onPlayerEndTrade(Player* player)
{
	if (playerEndTradeEvent == -1) {
		return;
	}

	// onPlayerEndTrade(player)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onPlayerEndTrade] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(playerEndTradeEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(playerEndTradeEvent);
	tfs::lua::pushUserdata(L, player);
	tfs::lua::setMetatable(L, -1, "Player");
	scriptInterface->callFunction(1);
}

void NpcEventsHandler::onThink()
{
	if (thinkEvent == -1) {
		return;
	}

	// onThink()
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onThink] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(thinkEvent, scriptInterface.get());
	env->setNpc(npc);

	scriptInterface->pushFunction(thinkEvent);
	scriptInterface->callFunction(0);
}

void NpcEventsHandler::onCreatureSight(Creature* creature)
{
	if (creatureSightEvent == -1) {
		return;
	}

	// onCreatureSight(creature)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onCreatureSight] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(creatureSightEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(creatureSightEvent);
	tfs::lua::pushUserdata(L, creature);
	tfs::lua::setCreatureMetatable(L, -1, creature);
	scriptInterface->callFunction(1);
}

void NpcEventsHandler::onSpeechBubble(Player* player, uint8_t& speechBubble)
{
	if (speechBubbleEvent == -1) {
		return;
	}

	// onSpeechBubble(player, speechBubble)
	if (!tfs::lua::reserveScriptEnv()) {
		std::cout << "[Error - NpcScript::onSpeechBubble] Call stack overflow" << std::endl;
		return;
	}

	ScriptEnvironment* env = tfs::lua::getScriptEnv();
	env->setScriptId(speechBubbleEvent, scriptInterface.get());
	env->setNpc(npc);

	lua_State* L = scriptInterface->getLuaState();
	scriptInterface->pushFunction(speechBubbleEvent);
	tfs::lua::pushUserdata(L, player);
	tfs::lua::setMetatable(L, -1, "Player");
	lua_pushnumber(L, speechBubble);

	if (tfs::lua::protectedCall(L, 2, 1) != 0) {
		reportErrorFunc(L, tfs::lua::popString(L));
	} else {
		speechBubble = tfs::lua::getNumber<uint8_t>(L, -1);
		lua_pop(L, 1);
	}
	tfs::lua::resetScriptEnv();
}