// void, Obj This

// TusMulas: the hero gathers food wagons until the ones following it carry 2001 food.
// 1. Free wagons of its player, nearest ring first (400, 800, 1200, 1600).
// 2. If that isn't enough, its nearest town halls and villages send 500-food wagons.
// It runs when the "tusmulas" order marks the hero (group "tusmulas_now"), or on its own in automatic mode
// (group "tusmulas_auto") when the wagons following it carry less than autoLow food.
// It's a behavior: it runs on every PC with the same data and only reads the map, so it stays in sync.

Hero hero;
Wagon w;
Obj o;
ObjList all, bases;
Settlement s;
point firstPos;
str heroGroup, cmd, msg;
int player, nHeroes, heroId, i, j, ring, d, bestDist;
int total, following, recruited, needed, sent, before;
int goal, wagonFood, ringSize, goldType, autoLow, autoEvery, autoCooldown;
int tick, cooldown;
bool ok, now, autoCheck;

goal = 2001;
wagonFood = 500;
ringSize = 400;
// Wagon::restype of a gold wagon (the value the DLL version skipped at +0x1d4)
goldType = 1;
// Automatic mode: it checks every autoEvery half-seconds and waits autoCooldown half-seconds after gathering
autoLow = 1000;
autoEvery = 10;
autoCooldown = 60;

Sleep(rand(500) + 100);
hero = This.AsHero();
tick = 0;
cooldown = 0;

while (hero.IsValid() && hero.IsAlive()) {
	Sleep(500);
	tick += 1;
	if (cooldown > 0)
		cooldown -= 1;

	now = hero.IsInGroup("tusmulas_now");
	if (now)
		hero.RemoveFromGroup("tusmulas_now");
	autoCheck = !now && cooldown == 0 && tick % autoEvery == 0 && hero.IsInGroup("tusmulas_auto");

	if (now || autoCheck) {
		player = hero.player;

		// The engine doesn't say whom a wagon follows, so each hero gets a group "tusmulas_h<n>"
		// and the wagons it called go in "tusmulas_w<n>". Groups and mod variables are saved with the game.
		nHeroes = Mod_GetInt("tusmulas_heroes");
		heroId = 0;
		for (i = 1; i <= nHeroes; i += 1) {
			if (heroId == 0 && hero.IsInGroup("tusmulas_h" + i))
				heroId = i;
		}
		if (heroId == 0) {
			heroId = Mod_AddInt("tusmulas_heroes", 1);
			nHeroes = heroId;
			hero.AddToGroup("tusmulas_h" + heroId);
		}
		heroGroup = "tusmulas_w" + heroId;

		// Food already following this hero. A wagon that was given another order leaves the group
		all = GetPlayerUnits(player);
		total = 0;
		following = 0;
		for (i = 0; i < all.count; i += 1) {
			w = all[i].AsWagon();
			if (w.IsValid() && w.IsAlive() && w.IsInGroup(heroGroup)) {
				if (w.command == "follow" && w.restype != goldType) {
					total += w.amount;
					following += 1;
				} else {
					w.RemoveFromGroup(heroGroup);
				}
			}
		}

		if (now || total < autoLow) {
			if (autoCheck)
				Mod_Log("TusMulas: automatic mode, food is low");
			Mod_LogInt("TusMulas: wagons already following", following);
			Mod_LogInt("TusMulas: food already following", total);

			// Free wagons with food, ring by ring
			recruited = 0;
			for (ring = 1; ring <= 4; ring += 1) {
				for (i = 0; i < all.count; i += 1) {
					if (total < goal) {
						w = all[i].AsWagon();
						if (w.IsValid() && w.IsAlive() && w.restype != goldType && w.amount > 0 && !w.IsInGroup(heroGroup)) {
							cmd = w.command;
							if (cmd == "idle" || cmd == "move" || cmd == "unload" || cmd == "load") {
								d = w.DistTo(hero);
								if ((ring == 1 || d > (ring - 1) * ringSize) && d <= ring * ringSize) {
									// It may have been called by another hero before
									for (j = 1; j <= nHeroes; j += 1)
										w.RemoveFromGroup("tusmulas_w" + j);
									w.SetCommand("follow", hero);
									w.AddToGroup(heroGroup);
									total += w.amount;
									recruited += 1;
								}
							}
						}
					}
				}
			}
			Mod_LogInt("TusMulas: free wagons called", recruited);

			// New wagons from the nearest settlements, as many as each one's food allows
			sent = 0;
			if (total < goal) {
				needed = (goal - total + wagonFood - 1) / wagonFood;
				bases = Union(ClassPlayerObjs("BaseTownhall", player), ClassPlayerObjs("BaseVillage", player)).GetObjList();
				while (sent < needed && bases.count > 0) {
					j = 0;
					bestDist = bases[0].DistTo(hero);
					for (i = 1; i < bases.count; i += 1) {
						d = bases[i].DistTo(hero);
						if (d < bestDist) {
							j = i;
							bestDist = d;
						}
					}
					o = bases[j];
					bases.Remove(o);
					s = o.AsBuilding().settlement;
					ok = s.IsValid();
					while (ok && sent < needed && s.food >= wagonFood) {
						before = s.food;
						w = s.CreateMuleFood(wagonFood);
						if (!w.IsValid()) {
							ok = false;
						} else {
							// The DLL version saw the engine take the food; if this call doesn't, take it here
							if (s.food > before - wagonFood)
								s.SetFood(before - wagonFood);
							w.SetCommand("follow", hero);
							w.AddToGroup(heroGroup);
							if (sent == 0)
								firstPos = w.pos;
							total += wagonFood;
							sent += 1;
						}
					}
				}
				if (sent < needed)
					Mod_LogInt("TusMulas: wagons missing for lack of food in the settlements", needed - sent);
			}
			Mod_LogInt("TusMulas: new wagons sent", sent);
			Mod_LogInt("TusMulas: food following the hero now", total);

			if (sent > 0) {
				if (sent == 1)
					msg = Translate("1 food wagon has been sent.");
				else
					msg = Translatef("%s1 food wagons have been sent.", sent);
				UserNotification("location message", msg, firstPos, player);
			}
			cooldown = autoCooldown;
		}
	}
}

// A behavior script must not end
while(true) Sleep(50000);
