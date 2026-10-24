# TusMulas (mod de scripts)

La DLL TusMulas reescrita como mod de Imperivm Resurrected: sin DLL, sin hilo propio y sin direcciones de `gbr.exe`. Es una orden nueva del héroe, con su botón y su tecla (`I`), que hace lo mismo que la tecla `Y` de la DLL:

1. Suma la comida de los carros que ya siguen al héroe. Si llega a 2001, no hace nada más.
2. Llama a los carros de víveres libres del jugador (en `idle`, `move`, `load` o `unload`) por anillos: hasta 400, hasta 800, hasta 1200 y hasta 1600. Para cuando la suma llega a 2001.
3. Si no basta, los ayuntamientos y aldeas del jugador, del más cercano al más lejano, crean carros de 500 (`Settlement::CreateMuleFood`) mientras les quede comida. Los carros nuevos también siguen al héroe.
4. Si ha salido algún carro nuevo, avisa con la notificación `location message` en el sitio del primero.

Cada paso deja una línea en `Logs\imperivm\imperivm-mod.log` (`script: TusMulas: …`).

| Archivo | Qué es |
|---|---|
| `data\commands\TusMulas.xml` | La orden: botón, tecla `I` y nombre. Va a todos los héroes (`<src obj="Hero"/>`). |
| `patches\data\classes\Hero.sc.xml` | Añade el método `tusmulas` a la clase `Hero` con un parche. |
| `data\mod\tusmulas.vs` | El script que hace todo. |
| `loc\spanish.loc.xml`, `loc\italian.loc.xml` | Los textos en español e italiano. |

## Diferencias con la DLL

| DLL | Mod |
|---|---|
| Un hilo propio mira la tecla `Y` con `GetAsyncKeyState` y llama al motor desde fuera de su hilo. | Es una orden del juego: el script corre dentro del hilo del juego, cuando toca. |
| La `Y` ya es la orden `skill_battle_cry` del héroe, y salta aunque estés escribiendo en el chat. | Tecla `I`, la única libre para los héroes. Se cambia en **Opciones → Controles → Contenido**. |
| Jugador: `game+0x1514` (un campo del jugador 0). | El jugador del héroe (`hero.player`). |
| Saber si un carro ya te sigue: leer `+0x110`, `+0x30` y `+0x148`. | El motor no lo dice a los scripts. Cada héroe tiene un grupo `tusmulas_h<n>` y sus carros van en `tusmulas_w<n>`. Los grupos se guardan con la partida. Si a un carro le das otra orden, sale del grupo la próxima vez. |
| Edificios por nombre de plantilla (`village`, `townhall`…). | `BaseTownhall` y `BaseVillage`: los puestos (`Outpost`) no cuentan. |
| Guardado rápido (F6) antes de cada uso, por si se colgaba. | No hace falta. |
| Consola y VEH para ver los cuelgues. | `Mod_Log` en el log del mod. |

Lo único que no se ha portado es cambiar el título de la notificación a "Caravana de Comida": eso era escribir en la plantilla del motor. Sale con el título de `location message`.

## Probarlo (hito 1, sin conexión)

1. Copia la carpeta `tusmulas` a `<juego>\mods\`, actívala en el gestor de contenido y reinicia.
2. Empieza una escaramuza. Selecciona un héroe y pulsa `I` (o su botón).
3. Mira `Logs\imperivm\imperivm-mod.log`. Si el script no compila, el error sale ahí como `script: compilation error: …` con línea y columna.

Qué comprobar, porque no lo he podido probar en el juego:

- **Que compile.** Lo más dudoso es `"tusmulas_h" + i` (texto más número): el lenguaje tiene la conversión de `int` a `str`, pero no lo he visto usado así.
- **El tipo de recurso del oro.** `goldType = 1` sale de tu DLL (`+0x1d4 == 1` era oro). Si los carros de comida se quedan quietos y los de oro van, cambia `goldType` a `0`.
- **Que el poblado pierda los 500.** Si `CreateMuleFood` no los descuenta, el script los quita a mano. Comprueba que la comida del poblado baja 500 por carro, no 1000.
- **La notificación.** El último número de `UserNotification` es el jugador. Tu DLL pasaba `0`. Si no sale el aviso, prueba `0` en la última línea.
- **La orden `follow`.** Es la misma que creaba tu DLL con `CommandFactory_Create`. Si los carros no se mueven, mira qué orden tienen en el log.

## Multijugador (hito 2)

Por cómo está hecho, debería funcionar ya en multijugador, sin cambios:

- Pulsar el botón o la tecla manda la orden por la red, como cualquier otra. Cada PC ejecuta el script en el mismo momento de la simulación.
- El script solo lee cosas iguales en todos los PC: objetos del mapa, comida de los poblados, grupos y variables del mod. No lee el ratón, la cámara, el jugador que mira ni el reloj, y no usa `rand`.
- Los grupos y `Mod_AddInt` cambian igual en todos los PC, porque solo los cambia este script.
- `Translate` y `UserNotification` solo cambian lo que se ve, no la partida.

Falta probarlo entre dos PC con la misma versión de Imperivm Resurrected y el mod activado en los dos. Si la partida se desincroniza al pulsar `I`, el log de cada PC dirá en qué paso estaba cada uno.
