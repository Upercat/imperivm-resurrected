// void, Obj me

// Turns automatic mode on or off for this hero. Its behavior (tusmulas_hero.vs) reads the group
if (me.IsInGroup("tusmulas_auto")) {
	me.RemoveFromGroup("tusmulas_auto");
	me.ShowFloatText(Translate("Automatic food: off"));
} else {
	me.AddToGroup("tusmulas_auto");
	me.ShowFloatText(Translate("Automatic food: on"));
}
