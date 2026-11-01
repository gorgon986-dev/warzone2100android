// Take a Break - commander
//
// Runs alongside the AI that plays for you during "Take a Break" and carries out
// typed commands (from the "Command AI" box). The AI keeps playing in between,
// so it may later re-task units after a command has been carried out.
//
// Commands (not case-sensitive, a number sets how many):
//   help                      - list the commands
//   build defenses [n]        - trucks build the strongest available defenses toward the enemy
//   build units / tanks [n]   - factories produce the strongest tanks available
//   build cyborgs [n]         - cyborg factories produce cyborgs
//   build trucks [n]          - factories produce trucks
//   build oil / derricks      - trucks build derricks on free oil (and power generators as needed)
//   attack                    - all combat units attack the nearest enemy base
//   retreat / defend          - all combat units return to base
//   research                  - idle labs research upgrades (and a new lab is built if there are few)
//   status                    - power, units and structures
//   auto on / auto off        - the commander decides on its own which of the above helps most

const CMD_TIMER_MS = 2500;

var pendingTanks = 0;
var pendingCyborgs = 0;
var pendingTrucks = 0;

// Auto mode: every so often, decide which of the commands would help most right now
const AUTO_TIMER_MS = 15000;
var autoMode = false;
var lastAutoAttack = 0;
var lastAutoFortify = 0;

var autoLog = null; // collects messages during an auto tick

function say(msg)
{
	if (autoLog !== null)
	{
		autoLog.push(msg);
		return;
	}
	console("[AI] " + msg);
}

function eventGameInit()
{
	setTimer("commanderTick", CMD_TIMER_MS);
	setTimer("autoTick", AUTO_TIMER_MS);
}

function eventStartLevel()
{
	// (the timer is set up in eventGameInit)
}

function eventChat(from, to, message)
{
	if (from !== me || !message)
	{
		return;
	}
	handleCommand(String(message));
}

function parseCount(text, fallback)
{
	const m = text.match(/(\d+)/);
	if (m)
	{
		return Math.max(1, Math.min(50, parseInt(m[1], 10)));
	}
	return fallback;
}

function handleCommand(raw)
{
	const text = raw.toLowerCase().trim();
	if (text === "" )
	{
		return;
	}
	if (/^auto/.test(text))
	{
		autoMode = /off|stop|disable/.test(text) ? false : (/on|start|enable/.test(text) ? true : !autoMode);
		say(autoMode ? "Auto: I'll decide what to focus on (defenses, research, units, oil, attacks)." : "Auto off.");
		if (autoMode)
		{
			autoTick();
		}
	}
	else if (/\b(help|commands|\?)\b/.test(text) || text === "?")
	{
		say("Commands: build defenses [n], build units [n], build cyborgs [n], build trucks [n], build oil, research, attack, retreat, status, auto on/off");
	}
	else if (/defen[cs]e|tower|bunker|hardpoint|turret/.test(text))
	{
		buildDefenses(parseCount(text, 3));
	}
	else if (/truck|builder|constructor|engineer/.test(text))
	{
		pendingTrucks += parseCount(text, 2);
		say("Building " + pendingTrucks + " truck(s).");
		commanderTick();
	}
	else if (/oil|derrick|power/.test(text))
	{
		buildOil();
	}
	else if (/cyborg|borg/.test(text))
	{
		pendingCyborgs += parseCount(text, 5);
		say("Building " + pendingCyborgs + " cyborg(s).");
		commanderTick();
	}
	else if (/unit|tank|army|troop|soldier|droid/.test(text))
	{
		pendingTanks += parseCount(text, 5);
		say("Building " + pendingTanks + " tank(s).");
		commanderTick();
	}
	else if (/attack|charge|push|assault|rush/.test(text))
	{
		attack();
	}
	else if (/retreat|fall back|defend|go home|return|regroup/.test(text))
	{
		retreat();
	}
	else if (/research|tech|upgrade|science/.test(text))
	{
		research();
	}
	else if (/status|report|how/.test(text))
	{
		status();
	}
	else
	{
		say("I don't know \"" + raw + "\". Type help for the list of commands.");
	}
}

// ---------------------------------------------------------------------------
// helpers

function baseLocation()
{
	const hq = enumStruct(me, HQ);
	if (hq.length > 0)
	{
		return { x: hq[0].x, y: hq[0].y };
	}
	const factories = enumStruct(me, FACTORY);
	if (factories.length > 0)
	{
		return { x: factories[0].x, y: factories[0].y };
	}
	const all = enumStruct(me);
	if (all.length > 0)
	{
		return { x: all[0].x, y: all[0].y };
	}
	if (startPositions[me])
	{
		return { x: startPositions[me].x, y: startPositions[me].y };
	}
	return { x: Math.floor(mapWidth / 2), y: Math.floor(mapHeight / 2) };
}

function isEnemy(player)
{
	return player !== me && !allianceExistsBetween(me, player);
}

function nearestEnemyTarget(from)
{
	let best = null;
	let bestDist = Infinity;
	for (let p = 0; p < maxPlayers; ++p)
	{
		if (!isEnemy(p))
		{
			continue;
		}
		const structs = enumStruct(p);
		for (let i = 0; i < structs.length; ++i)
		{
			const d = distBetweenTwoPoints(from.x, from.y, structs[i].x, structs[i].y);
			if (d < bestDist)
			{
				bestDist = d;
				best = { x: structs[i].x, y: structs[i].y };
			}
		}
	}
	if (best === null)
	{
		// no known structures - head for the nearest enemy start position
		for (let p = 0; p < maxPlayers; ++p)
		{
			if (!isEnemy(p) || !startPositions[p])
			{
				continue;
			}
			const d = distBetweenTwoPoints(from.x, from.y, startPositions[p].x, startPositions[p].y);
			if (d < bestDist)
			{
				bestDist = d;
				best = { x: startPositions[p].x, y: startPositions[p].y };
			}
		}
	}
	return best;
}

function trucks()
{
	return enumDroid(me).filter((d) => (d.droidType === DROID_CONSTRUCT || d.droidType === DROID_CYBORG_CONSTRUCT));
}

function combatUnits()
{
	return enumDroid(me).filter((d) => ((d.droidType === DROID_WEAPON || d.droidType === DROID_CYBORG || d.droidType === DROID_CYBORG_SUPER) && !isVTOL(d)));
}

function freeTrucks(max)
{
	// prefer trucks that are not busy building
	const all = trucks();
	const idle = all.filter((d) => (d.order !== DORDER_BUILD && d.order !== DORDER_HELPBUILD && d.order !== DORDER_LINEBUILD));
	const pool = idle.length > 0 ? idle : all;
	return pool.slice(0, max);
}

// ---------------------------------------------------------------------------
// best available components / structures

function bestDefense()
{
	let best = null;
	let bestPower = -1;
	for (const name in Stats.Building)
	{
		const s = Stats.Building[name];
		if (s.Type !== "Wall" || !s.Weapons || s.Weapons.length === 0)
		{
			continue;
		}
		if (/AASite|SAM|Sunburst|WallTower06|A0BaBa|bba|Scav|Pillbox-Cannon6-NEXUS|NEXUS/i.test(s.Id))
		{
			continue; // anti-air, scavenger and special structures
		}
		if (!isStructureAvailable(s.Id, me))
		{
			continue;
		}
		if (s.BuildPower > bestPower)
		{
			bestPower = s.BuildPower;
			best = s.Id;
		}
	}
	return best;
}

function bestBody(heavyPreferred)
{
	let best = null;
	let bestHP = -1;
	for (const name in Stats.Body)
	{
		const b = Stats.Body[name];
		if (!/^Body\d+/.test(b.Id) || !componentAvailable(b.Id))
		{
			continue;
		}
		const hp = heavyPreferred ? b.HitPoints : -b.BuildPower;
		if (hp > bestHP)
		{
			bestHP = hp;
			best = b.Id;
		}
	}
	return best;
}

function bestPropulsion()
{
	const prefs = ["tracked01", "HalfTrack", "hover01", "wheeled01"];
	for (let i = 0; i < prefs.length; ++i)
	{
		if (componentAvailable(prefs[i]))
		{
			return prefs[i];
		}
	}
	return "wheeled01";
}

function bestWeapon(forCyborg)
{
	let best = null;
	let bestScore = -1;
	for (const name in Stats.Weapon)
	{
		const w = Stats.Weapon[name];
		if (!w.Id || !componentAvailable(w.Id))
		{
			continue;
		}
		const isCyborgWeapon = /^Cyb|Cyborg/i.test(w.Id);
		if (forCyborg !== isCyborgWeapon)
		{
			continue;
		}
		if (/VTOL|Bomb|AAGun|SAM|Sunburst|Baba|Buggy|Scav|NEXUS|Mortar|Howitzer|IDF|MRL|Missile-HvyArt|Missile-MdArt|Rocket-BB/i.test(w.Id))
		{
			continue; // air-only, artillery and scavenger weapons
		}
		if (w.ShootOnGround === false)
		{
			continue;
		}
		const pause = Math.max(100, (w.FirePause || 0) + ((w.ReloadTime || 0) / Math.max(1, w.Rounds || 1)));
		const score = (w.Damage || 0) * 1000 / pause;
		if (score > bestScore)
		{
			bestScore = score;
			best = w.Id;
		}
	}
	return best;
}

// ---------------------------------------------------------------------------
// commands

function buildDefenses(count)
{
	const defense = bestDefense();
	if (!defense)
	{
		say("No defensive structures are researched yet.");
		return;
	}
	const workers = freeTrucks(Math.max(1, Math.min(count, 4)));
	if (workers.length === 0)
	{
		say("I have no trucks to build defenses. Try: build trucks");
		return;
	}
	const base = baseLocation();
	const enemy = nearestEnemyTarget(base);
	// place them between the base and the enemy, a few tiles out
	let cx = base.x;
	let cy = base.y;
	if (enemy)
	{
		const d = Math.max(1, distBetweenTwoPoints(base.x, base.y, enemy.x, enemy.y));
		const out = Math.min(8, d / 3);
		cx = Math.round(base.x + (enemy.x - base.x) * out / d);
		cy = Math.round(base.y + (enemy.y - base.y) * out / d);
	}
	let ordered = 0;
	for (let i = 0; i < count; ++i)
	{
		const truck = workers[i % workers.length];
		const spread = (i - Math.floor(count / 2)) * 2;
		const pos = pickStructLocation(truck, defense, cx + spread, cy - spread);
		if (pos && orderDroidBuild(truck, DORDER_BUILD, defense, pos.x, pos.y))
		{
			++ordered;
		}
	}
	if (ordered > 0)
	{
		say("Building " + ordered + " defense(s) toward the enemy.");
	}
	else
	{
		say("Couldn't find room to build defenses.");
	}
}

function buildOil()
{
	const oils = enumFeature(ALL_PLAYERS, "OilResource");
	const workers = freeTrucks(4);
	if (workers.length === 0)
	{
		say("I have no trucks. Try: build trucks");
		return;
	}
	const base = baseLocation();
	oils.sort((a, b) => distBetweenTwoPoints(base.x, base.y, a.x, a.y) - distBetweenTwoPoints(base.x, base.y, b.x, b.y));
	let ordered = 0;
	for (let i = 0; i < oils.length && ordered < workers.length; ++i)
	{
		const truck = workers[ordered];
		if (droidCanReach(truck, oils[i].x, oils[i].y) && orderDroidBuild(truck, DORDER_BUILD, "A0ResourceExtractor", oils[i].x, oils[i].y))
		{
			++ordered;
		}
	}
	// one power generator per four derricks
	const derricks = countStruct("A0ResourceExtractor") + ordered;
	const gens = countStruct("A0PowerGen");
	if (derricks > gens * 4 && isStructureAvailable("A0PowerGen", me) && workers.length > 0)
	{
		const pos = pickStructLocation(workers[0], "A0PowerGen", base.x, base.y);
		if (pos)
		{
			orderDroidBuild(workers[0], DORDER_BUILD, "A0PowerGen", pos.x, pos.y);
			say("Also building a power generator.");
		}
	}
	say(ordered > 0 ? ("Sending " + ordered + " truck(s) to build derricks.") : "No free oil within reach.");
}

function attack()
{
	const units = combatUnits();
	if (units.length === 0)
	{
		say("I have no combat units. Try: build units");
		return;
	}
	const target = nearestEnemyTarget(baseLocation());
	if (!target)
	{
		say("I can't find an enemy to attack.");
		return;
	}
	units.forEach((d) => { orderDroidLoc(d, DORDER_SCOUT, target.x, target.y); });
	say("Attacking with " + units.length + " unit(s)!");
}

function retreat()
{
	const units = combatUnits();
	units.forEach((d) => { orderDroid(d, DORDER_RTB); });
	say("Pulling " + units.length + " unit(s) back to base.");
}

function research()
{
	const labs = enumStruct(me, RESEARCH_LAB).filter((l) => (l.status === BUILT && structureIdle(l)));
	const available = enumResearch();
	// upgrades first (weapons, armour, research speed, power), then anything cheap
	const isUpgrade = (r) => /Upgrade|Damage|ROF|Acc|Metals|Armor|Armour|Cyborg-Metals|Power|Research|Struc-Materials|Defense-WallUpgrade/i.test(r.id);
	available.sort((a, b) => {
		const ua = isUpgrade(a) ? 0 : 1;
		const ub = isUpgrade(b) ? 0 : 1;
		if (ua !== ub)
		{
			return ua - ub;
		}
		return a.power - b.power;
	});
	let started = 0;
	for (let i = 0; i < labs.length && i < available.length; ++i)
	{
		if (pursueResearch(labs[i], available[i].id))
		{
			++started;
		}
	}
	// add a research facility if there are few
	const labCount = enumStruct(me, RESEARCH_LAB).length;
	if (labCount < 5 && isStructureAvailable("A0ResearchFacility", me))
	{
		const workers = freeTrucks(1);
		if (workers.length > 0)
		{
			const base = baseLocation();
			const pos = pickStructLocation(workers[0], "A0ResearchFacility", base.x, base.y);
			if (pos && orderDroidBuild(workers[0], DORDER_BUILD, "A0ResearchFacility", pos.x, pos.y))
			{
				say("Building another research facility.");
			}
		}
	}
	say(started > 0 ? ("Started " + started + " research project(s), upgrades first.") : (labs.length === 0 ? "All research facilities are busy." : "Nothing new to research right now."));
}

function status()
{
	say("Power " + playerPower(me) + ", " + combatUnits().length + " combat units, " + trucks().length + " trucks, " +
		enumStruct(me, FACTORY).length + " factories, " + countStruct("A0ResourceExtractor") + " derricks." +
		(pendingTanks + pendingCyborgs + pendingTrucks > 0 ? (" Queued: " + pendingTanks + " tanks, " + pendingCyborgs + " cyborgs, " + pendingTrucks + " trucks.") : ""));
}

// ---------------------------------------------------------------------------
// production (runs on a timer, using idle factories)

function commanderTick()
{
	if (pendingTrucks > 0 || pendingTanks > 0)
	{
		const factories = enumStruct(me, FACTORY).filter((f) => (f.status === BUILT && structureIdle(f)));
		for (let i = 0; i < factories.length; ++i)
		{
			if (pendingTrucks > 0)
			{
				if (buildDroid(factories[i], "Truck", bestBody(false) || "Body1REC", bestPropulsion() === "tracked01" ? "HalfTrack" : bestPropulsion(), "", "", "Spade1Mk1"))
				{
					--pendingTrucks;
				}
			}
			else if (pendingTanks > 0)
			{
				const weapon = bestWeapon(false);
				if (!weapon)
				{
					say("No tank weapons are researched yet.");
					pendingTanks = 0;
					break;
				}
				if (buildDroid(factories[i], "Tank", bestBody(true) || "Body1REC", bestPropulsion(), "", "", weapon))
				{
					--pendingTanks;
				}
			}
		}
	}
	if (pendingCyborgs > 0)
	{
		const factories = enumStruct(me, CYBORG_FACTORY).filter((f) => (f.status === BUILT && structureIdle(f)));
		if (factories.length === 0 && enumStruct(me, CYBORG_FACTORY).length === 0)
		{
			say("I have no cyborg factory.");
			pendingCyborgs = 0;
		}
		for (let i = 0; i < factories.length && pendingCyborgs > 0; ++i)
		{
			const weapon = bestWeapon(true);
			if (!weapon)
			{
				say("No cyborg weapons are researched yet.");
				pendingCyborgs = 0;
				break;
			}
			// super cyborg weapons need the heavy body
			const body = /Hvy|Super/i.test(weapon) ? "CyborgHeavyBody" : "CyborgLightBody";
			if (buildDroid(factories[i], "Cyborg", body, "CyborgLegs", "", "", weapon))
			{
				--pendingCyborgs;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// auto mode

function enemyUnitsNear(pos, radius)
{
	let count = 0;
	for (let p = 0; p < maxPlayers; ++p)
	{
		if (!isEnemy(p))
		{
			continue;
		}
		count += enumRange(pos.x, pos.y, radius, p, false).filter((o) => (o.type === DROID)).length;
	}
	return count;
}

function freeOilNear(pos, radius)
{
	return enumFeature(ALL_PLAYERS, "OilResource").filter((o) => (distBetweenTwoPoints(pos.x, pos.y, o.x, o.y) <= radius)).length;
}

function autoTick()
{
	if (!autoMode)
	{
		return;
	}
	autoLog = [];
	try
	{
		autoDecide();
	}
	finally
	{
		// only report things that actually happened
		const done = autoLog.filter((m) => !/^(No |Nothing|All research|I have no|I can't|Couldn't|I don't)/.test(m));
		autoLog = null;
		if (done.length > 0)
		{
			console("[AI auto] " + done.join(" "));
		}
	}
}

function autoDecide()
{
	const base = baseLocation();
	const threat = enemyUnitsNear(base, 14);
	const army = combatUnits().length;

	// 1. under attack: bring the army home and fortify
	if (threat >= 3)
	{
		retreat();
		if (gameTime - lastAutoFortify > 45000)
		{
			lastAutoFortify = gameTime;
			buildDefenses(2);
		}
		return;
	}

	// 2. keep the economy growing
	if (trucks().length > 0 && freeOilNear(base, 30) > 0 && countStruct("A0ResourceExtractor") < 40)
	{
		buildOil();
	}

	// 3. keep the labs busy
	const idleLabs = enumStruct(me, RESEARCH_LAB).filter((l) => (l.status === BUILT && structureIdle(l)));
	if (idleLabs.length > 0)
	{
		research();
	}

	// 4. spend spare power on units (and trucks if few)
	if (trucks().length < 4 && pendingTrucks === 0)
	{
		pendingTrucks = 2;
	}
	if (playerPower(me) > 500 && pendingTanks < 3)
	{
		pendingTanks += 3;
	}

	// 5. attack with a big enough army (not too often)
	if (army >= 20 && gameTime - lastAutoAttack > 120000)
	{
		lastAutoAttack = gameTime;
		attack();
	}
	// 6. a little extra defense now and then
	else if (gameTime - lastAutoFortify > 180000 && playerPower(me) > 800)
	{
		lastAutoFortify = gameTime;
		buildDefenses(1);
	}
}
