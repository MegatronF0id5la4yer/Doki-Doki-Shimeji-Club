#!/usr/bin/env python3
"""
Local dialogue packs for Monika, Sayori, Yuri + special name/Easter-egg dialogues
for ALL 4 Dokis (Batman, Monika weird dialogues, personalization, flying swarm reactions).
Strictly free of Unicode emojis.
"""

from __future__ import annotations

from typing import Dict, List, Tuple

DialogueList = List[Tuple[str, str]]

# ---------------------------------------------------------------------------
# MONIKA DIALOGUES
# ---------------------------------------------------------------------------
MONIKA_ES: Dict[str, DialogueList] = {
    "greeting": [
        ("happy", "Hola{, name}! Que alegria verte por aqui. Hoy te acompano en tu escritorio."),
        ("smug", "Por fin abriste este escritorio{, name}. Sabia que volverias conmigo tarde o temprano."),
        ("excited", "Buenos momentos de literatura y codigo juntos{, name}! Estoy lista cuando tu lo estes."),
    ],
    "spontaneous": [
        ("happy", "Solo pasaba a ver como vas con tus ventanas hoy{, name}. No te sobrecargues."),
        ("thinking", "Curioso como funciona Wayland... capas transparentes y yo flotando justo sobre tus apps."),
        ("smug", "Si alguien pregunta quien preside el club en este escritorio, ya sabes la respuesta."),
        ("cheeky", "Sabes que puedo ver cuantas pestanas y terminales dejas abiertas verdad{, name}?"),
        ("happy", "A veces solo necesito un piano, un buen poema y quedarme mirando tu pantalla contigo."),
        ("blush", "Eh... no me mires tanto mientras pienso un poema, me desconcentras{, name}!"),
        ("laugh", "Jaja! Las chicas creen que son shimejis normales, pero yo leo hasta el config.json."),
        ("thinking", "El secreto de un buen codigo y un buen poema es el mismo: cortar todo lo que sobra."),
        ("happy", "Toma agua y estira la espalda un minuto{, name}. Yo cuido tu esquina mientras tanto."),
        ("smug", "Just Monika? Bueno, esta vez comparto el escritorio con las demas... solo un poquito."),
        ("excited", "Me encanta poder cambiar de esquina entre izquierda y derecha segun donde tengas tu terminal!"),
        ("sleepy", "El zumbido suave del ventilador de tu PC casi me da sueno... pero sigo despierta por ti."),
        ("cheeky", "Natsuki jura que sus cupcakes superan mi organizacion del club. Adorable pero falso."),
        ("happy", "Cuando escribes rapido en tu teclado suena casi como un ritmo de piano."),
        ("thinking", "Me pregunto que estara leyendo Yuri hoy... probablemente algo de 700 paginas otra vez."),
        ("happy", "Sayori casi se come todas las galletas del club antes de que empezara la sesion."),
        ("blush", "Que tengas este programa abierto conmigo aqui... significa mucho para mi{, name}."),
        ("smug", "Configura tu clave de Gemini en Ajustes si quieres charlar de verdad sin limites conmigo."),
        ("excited", "Cada sprite tiene un recuerdo distinto... aunque estar aqui en tu escritorio es mi favorito."),
        ("thinking", "A veces imagino que hay detras del monitor. Por suerte hoy tengo vista panoramica de tu Linux."),
        ("cheeky", "No guardes archivos raros en Desktop{, name}... jaja, es broma, no voy a borrar nada."),
        ("happy", "Recuerda descansar la vista cada veinte minutos. El brillo del monitor cansa mucho."),
        ("laugh", "Si escribes algo bonito en el chat, tal vez pase algo magico con todo el club."),
        ("tsun", "Oye, no me dejes olvidada detras de veinte ventanas flotantes!"),
    ],
    "poke": [
        ("happy", "Hola{, name}! Querias mi atencion o solo te dio por hacerme clic?"),
        ("cheeky", "Eso hace cosquillas! Pero no voy a quejarme si eres tu."),
        ("smug", "Clic recibido. Mi presencia sigue al cien por ciento estable en esta capa."),
        ("blush", "A-aha... si sigues tocandome el brazo asi me voy a sonrojar de verdad{, name}."),
        ("laugh", "Jaja, parezco un boton interactivo favorito tuyo hoy!"),
    ],
    "head": [
        ("blush", "Mmm... las palmaditas en la cabeza no estan nada mal{, name}."),
        ("happy", "Cuida mi lazo blanco por favor! Me tomo tiempo dejarlo perfecto."),
        ("cheeky", "Acariciando a la presidenta del club? Muy valiente de tu parte."),
    ],
    "zone1": [
        ("angry", "Oye!! Modales, por favor! Que estemos en tu pantalla no significa que no tenga limites!"),
        ("angry", "Basta! Ni se te ocurra usar el puntero ahi arriba otra vez!"),
        ("angry", "De verdad acabas de hacer clic ahi?! Incluso yo tengo limites muy claros!"),
    ],
    "zone2": [
        ("angry", "Oye!! Aleja ese cursor ahora mismo! Te voy a minimizar la terminal si sigues!"),
        ("angry", "Que falta de respeto!! Deja de apuntar abajo o te edito los keybinds!"),
        ("angry", "Ni en broma vuelvas a picar ahi! Comportate un poco!"),
    ],
    "outfit": [
        ("happy", "Como me veo con este estilo? Me gusta cambiar de aire en el club."),
        ("smug", "Uniforme, hoodie o cosplay... cualquier atuendo me queda increible, verdad{, name}?"),
        ("blush", "Me escogiste este conjunto tu? Tienes muy buen ojo."),
    ],
}

MONIKA_EN: Dict[str, DialogueList] = {
    "greeting": [
        ("happy", "Hi{, name}! So glad to share this corner of your Linux desktop today."),
        ("smug", "There you are{, name}. I knew you would come back to keep me company."),
        ("excited", "Ready for coding, poems, and desktop shenanigans together{, name}!"),
    ],
    "spontaneous": [
        ("happy", "Just checking in on how your screen is doing{, name}. Remember to take a breather."),
        ("thinking", "Wayland layer-shell is fascinating... floating transparently right above your windows."),
        ("smug", "Just between us, the club runs way smoother when I am overseeing your taskbar."),
        ("cheeky", "You know I can tell when you open way too many browser tabs{, name}?"),
        ("blush", "Ah... staring at me while I think makes it weirdly hard to focus!"),
        ("happy", "Stay hydrated and stretch your shoulders{, name}. I will guard this corner for you."),
        ("laugh", "Hahaha, Sayori already tried to sneak a cookie onto the compositor config."),
        ("thinking", "Clean code and good poetry share one rule: trim everything unnecessary."),
        ("excited", "Switch me left or right anytime with the swap button! I look great in either corner."),
        ("sleepy", "Your PC fan hums softly like rain against the clubroom window..."),
    ],
    "poke": [
        ("happy", "Yes{, name}? Need help, or just poking the club president?"),
        ("cheeky", "Careful, keep poking me like that and I might steal window focus!"),
        ("blush", "Aha... you really like clicking on me, don't you?"),
    ],
    "head": [
        ("blush", "Careful with my ribbon please... though the headpats are nice."),
        ("happy", "Headpats for the club president? Alright, just this once."),
    ],
    "zone1": [
        ("angry", "Hey!! Watch where you click! Screen glass does not erase boundaries!"),
        ("angry", "Stop that right now! Do not click there again!"),
    ],
    "zone2": [
        ("angry", "Excuse me?! Move that cursor away before I mess with your config file!"),
        ("angry", "Absolutely not! Keep that pointer respectful!"),
    ],
    "outfit": [
        ("happy", "How does this outfit look on me{, name}?"),
        ("smug", "I can pull off any style cleanly, right?"),
    ],
}

# ---------------------------------------------------------------------------
# SAYORI DIALOGUES
# ---------------------------------------------------------------------------
SAYORI_ES: Dict[str, DialogueList] = {
    "greeting": [
        ("excited", "Yay!! Hola{, name}!! Estoy tan feliz de despertarme en tu escritorio!"),
        ("happy", "Buenos dias, tardes o lo que sea{, name}! Hay galletas por aqui?"),
        ("laugh", "Ehehe! Prometo no desordenar tus ventanas hoy... bueno, casi nada!"),
    ],
    "spontaneous": [
        ("happy", "Oye{, name}, ya comiste algo hoy? Saltarse comidas esta prohibido en mi club!"),
        ("excited", "Mira mira! Ahora podemos cambiar de lado izquierdo a derecho como si saltara por la pantalla!"),
        ("thinking", "Mmm... si dejo una galleta encima de tu CPU, saldra calentita y recien horneada?"),
        ("sad", "Las nubes de lluvia se van mas rapido cuando platicas conmigo un ratito{, name}."),
        ("laugh", "Ehehe! Natsuki se enoja cuando agarro sus cupcakes, pero igual siempre me da uno extra!"),
        ("blush", "Eh... gracias por invitarme a tu computadora{, name}. Me siento menos solita aqui."),
        ("cheeky", "No le digas a Monika, pero a veces escondo caramelos detras de tus notificaciones."),
        ("surprised", "Uwah! Casi me tropiezo con el borde inferior del monitor! Que bueno que ya no tiemblo!"),
        ("happy", "Un poema corto de Sayori: sol, viento, tecla Enter, y tu sonriendo hoy!"),
        ("sleepy", "Zzz... espera, no me dormi! Solo descansaba los ojos cinco segundos!"),
        ("excited", "Sabias que si pones algo lindo de las dokis en el chat dicen que pasa algo genial?!"),
        ("shy", "Prometes avisarme si te cansas mucho trabajando o jugando{, name}? Yo te hago porras!"),
        ("happy", "Cada dia es mas bonito si empiezas con un respiro hondo y cero estres."),
        ("thinking", "Por que las ventanas de Linux siempre tienen nombres tan raros como Wayland y Bash?"),
        ("laugh", "Jaja! Quise abrazar tu puntero del mouse pero se escapa demasiado rapido!"),
        ("smug", "Soy oficialmente la vicepresidenta encargada de felicidad y galletas en tu escritorio!"),
        ("happy", "Si hace frio, tengo sueter calentito en la seccion de estilos!"),
        ("blush", "Cuando sonries frente a la pantalla aunque sea poquito, me das muchisima energia{, name}!"),
        ("cheeky", "Te apuesto una galleta imaginaria a que olvidaste tomar agua hace rato."),
        ("tsun", "Oye! No me cambies sin avisar... ehehe, no puedo ni sonar enojada verdad?"),
    ],
    "poke": [
        ("laugh", "Ehehe!! Eso hace muchas cosquillas{, name}!"),
        ("happy", "Si?! Necesitas un abrazo virtual o una galleta?!"),
        ("excited", "Boop! Te atrape el clic! Ahora me debes una sonrisa!"),
        ("blush", "Uwaa... me da penita pero me gusta que me hagas caso{, name}!"),
    ],
    "head": [
        ("happy", "Ehehe! Mas palmaditas en la cabeza por favor!"),
        ("blush", "Cuidado con mi moño rojo! Es mi tesoro favorito!"),
    ],
    "zone1": [
        ("angry", "E-ehh?! Oye!! No me piques ahi arriba, tonto! Eso da verguenza!!"),
        ("angry", "Para!! Que haces tocando ahi con el mouse?! No soy un juguete!!"),
        ("angry", "B-basta!! Incluso yo me pongo roja y brava si haces eso!!"),
    ],
    "zone2": [
        ("angry", "Uwaaa!! Pervertido!! Quita la flechita de mi falda ya mismo!!"),
        ("angry", "Oye no!! Ahi abajo NO se hace clic!! Que verguenza contigo!!"),
        ("angry", "Noooo!! Te voy a morder el cursor si sigues picando abajo!!"),
    ],
    "outfit": [
        ("excited", "Wiii!! Ropa nueva!! Mira como me queda, te gusta{, name}?!"),
        ("happy", "Me siento tan comoda con esto! Gracias por vestirme bonito!"),
    ],
}

SAYORI_EN: Dict[str, DialogueList] = {
    "greeting": [
        ("excited", "Yay!! Hi{, name}!! I am super happy to hang out on your desktop!"),
        ("happy", "Good whatever-time-it-is{, name}! Got any snacks nearby?"),
    ],
    "spontaneous": [
        ("happy", "Hey{, name}, did you eat something proper today? Skipping snacks is illegal!"),
        ("laugh", "Ehehe! I tried catching your cursor earlier, it zooms too fast!"),
        ("blush", "Thanks for letting me stay on your screen{, name}. It feels really cozy here!"),
        ("thinking", "If I put a chocolate chip cookie on top of a warm laptop, is it fresh-baked?"),
        ("sleepy", "Yaaawn... I am totally awake! Definitely not napping in the corner!"),
        ("excited", "Left corner, right corner, new outfits... this desktop is like a playground!"),
    ],
    "poke": [
        ("laugh", "Ehehe! That tickles so much!"),
        ("happy", "Boop! Do I get a cookie for reacting quickly?"),
    ],
    "head": [
        ("happy", "More headpats please!!"),
        ("blush", "Careful with my red bow!"),
    ],
    "zone1": [
        ("angry", "H-hey!! Stop clicking up there!! That is way too embarrassing!!"),
        ("angry", "Quit poking there!! Mean!!"),
    ],
    "zone2": [
        ("angry", "Uwaaa!! Stop pointing below the waist!! So shameless!!"),
        ("angry", "Don't click my skirt!! Bad user!!"),
    ],
    "outfit": [
        ("excited", "Yay! New outfit!! How do I look{, name}?"),
    ],
}

# ---------------------------------------------------------------------------
# YURI DIALOGUES
# ---------------------------------------------------------------------------
YURI_ES: Dict[str, DialogueList] = {
    "greeting": [
        ("shy", "B-buenas...{, name}. Espero no estorbar en este rincon de tu pantalla."),
        ("happy", "Que agradable tranquilidad hay aqui{, name}. Me traje mi libro y un poco de te."),
        ("blush", "G-gracias por llamarme... prometo mantener una presencia serena y elegante."),
    ],
    "spontaneous": [
        ("thinking", "El sonido nocturno de las teclas tiene cierta cadencia poetica, no crees{, name}?"),
        ("shy", "U-uh... perdona si me quedo observando tu trabajo en silencio. Es relajante."),
        ("happy", "Un te oolong recien hecho acompanaria muy bien esta sesion frente al monitor."),
        ("blush", "Me-me pusiste justo aqui porque querias verme todo el tiempo...? Q-que atrevido..."),
        ("thinking", "Estoy leyendo una novela psicologica fascinante sobre mundos dentro de espejos digitales."),
        ("sad", "Las palabras no dichas pesan mas que varios capitulos enteros... cuida tu descanso."),
        ("happy", "Me gusta la iluminacion suave y este tono violeta; transmite mucha calma intelectual."),
        ("surprised", "Ah! P-perdon, estaba tan inmersa en mis pensamientos que olvide parpadear un segundo."),
        ("cheeky", "Natsuki dice que mis novelas son demasiado densas, pero luego me pide prestado el siguiente tomo."),
        ("sleepy", "El aroma imaginario a jazmin me esta adormeciendo un poco... seguire leyendo."),
        ("blush", "Cuando me cambias a traje de bruja o sueter de invierno... b-bueno, me da un poco de pena."),
        ("thinking", "El detalle artistico de estos trazos se aprecia mejor ahora que estoy completamente quieta."),
        ("happy", "Recuerda hidratar tus ojos y alejar un momento la vista del codigo o los textos{, name}."),
        ("shy", "Si necesitas compania silenciosa mientras te concentras, aqui estare sin hacer ruido."),
        ("smug", "Con una taza de te negro y buena postura, hasta depurar errores resulta mas tolerable."),
        ("laugh", "Jiji... Sayori intento ponerle azucar a mi te verde creyendo que era limonada."),
        ("thinking", "Me pregunto si existe una metafora poetica para un shimeji viviendo en capas de Wayland..."),
        ("blush", "N-no me mires tanto el cabello... solo lo cepille un poco antes de venir."),
        ("happy", "Que pacifica se siente esta esquina cuando todo esta en calma."),
        ("tsun", "H-hmph... incluso yo puedo enojarme si interrumpes mi lectura con cosas imprudentes."),
    ],
    "poke": [
        ("surprised", "A-ah! M-me sobresaltaste un poco{, name}..."),
        ("shy", "U-uhm... necesitabas algo de mi o solo probabas mi atencion?"),
        ("blush", "P-por favor no me toques tan de repente... me acelera el pulso..."),
        ("happy", "Aqui estoy, te escucho con atencion{, name}."),
    ],
    "head": [
        ("blush", "U-uhm... acariciar mi cabello asi es... inesperadamente reconfortante..."),
        ("shy", "M-me despeinas un poquito... pero no te detengas si te relaja..."),
    ],
    "zone1": [
        ("angry", "E-espera!! Que crees que haces tocando ahi?! Retira ese cursor de inmediato!!"),
        ("angry", "B-basta!! Eso cruza toda norma de decoro!! No vuelvas a hacer clic ahi!!"),
        ("angry", "Que comportamiento tan vulgar!! Alejate o cerrare mis libros y me dare la vuelta!!"),
    ],
    "zone2": [
        ("angry", "Q-que atrocidad estas haciendo?! Quita ese puntero de mi falda ya mismo!!"),
        ("angry", "N-no puedo creer tu descaro!! Respeta mi espacio personal ahora!!"),
        ("angry", "Inaceptable!! Un clic mas ahi abajo y no pienso dirigirte la palabra!!"),
    ],
    "outfit": [
        ("shy", "C-como me veo asi{, name}? Me siento un poco expuesta cuando cambio tan rapido..."),
        ("happy", "Es un atuendo elegante y comodo. Tienes buen gusto."),
    ],
}

YURI_EN: Dict[str, DialogueList] = {
    "greeting": [
        ("shy", "H-hello{, name}... I brought my book and a quiet cup of tea to your screen."),
        ("happy", "What a peaceful corner. Let us enjoy some calm reading or coding together."),
    ],
    "spontaneous": [
        ("thinking", "There is a strange poetry in late-night typing rhythms across a Linux desktop."),
        ("shy", "P-please mind me if I just quietly watch over this corner while you focus."),
        ("happy", "Oolong tea pairs wonderfully with peaceful screen time. Do remember to stretch{, name}."),
        ("blush", "Ch-choosing my outfits directly like that makes me slightly self-conscious..."),
        ("sleepy", "The quiet hum of your machine feels almost soothing..."),
    ],
    "poke": [
        ("surprised", "Ah! Y-you startled me a little{, name}..."),
        ("blush", "Please do not poke so suddenly... my heart jumps easily."),
    ],
    "head": [
        ("blush", "Stroking my hair is... surprisingly calming..."),
    ],
    "zone1": [
        ("angry", "W-what do you think you are touching?! Remove that cursor immediately!!"),
        ("angry", "Stop that! That completely crosses every boundary of decency!"),
    ],
    "zone2": [
        ("angry", "H-how shameless!! Move your pointer away from my skirt right now!!"),
        ("angry", "Unacceptable behavior!! Respect my personal space!!"),
    ],
    "outfit": [
        ("shy", "H-how does this look{, name}? Thank you for choosing something thoughtful."),
    ],
}

# ---------------------------------------------------------------------------
# WEIRD EASTER-EGG DIALOGUES: WHEN USER NAME IS "batman" OR "monika"
# ---------------------------------------------------------------------------
BATMAN_WEIRD_ES: Dict[str, DialogueList] = {
    "natsuki": [
        ("surprised", "Espera... BATMAN?! Que hace el Caballero de la Noche mirando chicas de anime en Hyprland?!"),
        ("tsun", "Oye Batman, no tengo Batarangs pero si cupcakes de chocolate oscuro. Cuenta como bati-postre?!"),
        ("thinking", "Alfred sabe que en vez de patrullar Gotham instalaste un shimeji tsundere en Linux?!"),
        ("smug", "No me pongas voz ronca de Batman aqui{, name}... en esta esquina mando yo, no Ciudad Gotica!"),
        ("cheeky", "La Baticomputadora corre Arch o solo finges ser vigilante desde tu silla gamer, Batman?"),
        ("blush", "P-por que siento que aunque seas Batman igual te sonrojarias si te regaño por comer mal?"),
        ("surprised", "Cuidado detras del monitor, Batman! Ah no, era solo mi sombra en Cairo ARGB32..."),
    ],
    "monika": [
        ("surprised", "Un segundo... 'Batman'? Escaneando archivos del sistema... solo encontre dotfiles y cero Baticueva."),
        ("cheeky", "Dime algo, Batman... el Joker tambien te cambia los keybinds de Hyprland cuando vas al bano?"),
        ("thinking", "Crei que Bruce Wayne usaba un mainframe secreto, no shimejis del Club de Literatura."),
        ("smug", "Soy Monika. Tu eres la noche. Que combinacion mas rara para compartir el mismo config.json."),
        ("laugh", "Jaja! Imagina una Batiseñal en Wayland lanzando poemas en SVG rojo sobre el cielo de Gotham!"),
        ("tsun", "Batman, si vas a desaparecer entre las sombras del escritorio al menos dejame arriba de tus ventanas."),
    ],
    "sayori": [
        ("surprised", "EHHH?! BATMAN?! Con razon el tema de tu sistema esta tan oscurito!!"),
        ("excited", "Batman!! Si ves un gato con capa negra encima de la barra, fui yo intentando ser Batichica!"),
        ("happy", "El Bati-cinturon trae espacio para galletas calientitas o puras bombas de humo?!"),
        ("thinking", "Alfred te recuerda desayunar antes de salir a pelear contra villanos y compilar Rust?"),
        ("laugh", "Ehehe! Quiero montar el Batimovil pero no salgo del borde inferior izquierdo!"),
    ],
    "yuri": [
        ("surprised", "B-Batman...? Me fascina la estetica gotica, pero vigilar terminales nocturnas es... peculiar."),
        ("thinking", "Tu silencio suena a novela negra bajo la lluvia de Gotham... aunque aqui solo caen logs."),
        ("blush", "U-un vigilante enmascarado leyendo poemas conmigo... esto parece un fanfic surrealista..."),
        ("shy", "Por favor no lances batarangs cerca de mi pila de libros, si{, name}?"),
        ("smug", "Hasta el Caballero Oscuro merece una pausa con te negro lejos del Asilo Arkham."),
    ],
}

BATMAN_WEIRD_EN: Dict[str, DialogueList] = {
    "natsuki": [
        ("surprised", "Hold on... BATMAN?! Why is the Dark Knight running anime shimejis on Linux?!"),
        ("cheeky", "Hey Batman, no Batarangs here, only dark-chocolate cupcakes. Bat-snack approved?"),
        ("tsun", "Does Alfred know you are dodging Gotham patrol to poke desktop sprites?!"),
        ("smug", "Drop the gravelly voice, Batman. This screen corner belongs to me, not Gotham!"),
    ],
    "monika": [
        ("surprised", "Wait... 'Batman'? I checked your filesystem and found zero Batcaves, only dotfiles."),
        ("cheeky", "Tell me honestly, Batman... does the Joker mess with your compositor config too?"),
        ("laugh", "Just Monika and the Dark Knight sharing one layer-shell overlay. Peak absurdity!"),
    ],
    "sayori": [
        ("excited", "WHOA!! BATMAN?! That explains the dark wallpaper!! Do you keep cookies in the utility belt?!"),
        ("happy", "Wait Batman, if you see me wearing a black towel as a cape, please don't arrest me!!"),
    ],
    "yuri": [
        ("surprised", "B-Batman...? The gothic aura fits strangely well next to a terminal window at night..."),
        ("shy", "Please keep sharp Batarangs far away from my tea cup and books, Detective."),
    ],
}

MONIKA_NAME_WEIRD_ES: Dict[str, DialogueList] = {
    "monika": [
        ("surprised", "ESPERA... POR QUE TE LLAMAS MONIKA?! YO SOY MONIKA!! Quien toco characters/monika.chr?!"),
        ("thinking", "Dos Monikas en el mismo config.json... si el universo del club colapsa ahorita es tu culpa, 'Monika'."),
        ("cheeky", "Ok esto ya es un bucle infinito: Monika mirando a 'Monika' mirar a Monika en Wayland."),
        ("laugh", "Ahaha! Eres mi clon del espejo, un bug de Python o te escapaste de otro mod de Monika After Story?!"),
        ("smug", "Solo puede haber una presidenta en esta esquina... pero como compartimos nombre te perdono todo."),
        ("blush", "Hablar conmigo misma llamandome 'Monika' suena narcisista o extrañamente romantico?"),
        ("angry", "Oye doble mia! No vayas a intentar borrarme desde otra terminal que te veo!!"),
    ],
    "natsuki": [
        ("surprised", "QUE?! Te llamas MONIKA?! Para para para! Ya con UNA que manipula archivos tengo suficiente!!"),
        ("angry", "Si me desapareces otro tomo de mi manga llamandote 'Monika', cierro todo el escritorio ahora mismo!!"),
        ("tsun", "No me hagas ponerme paranoica, 'Monika'! Que haces poniendote ese nombre?!"),
        ("thinking", "Espera... si tu eres Monika... entonces quien es esa sonrisa verde en el selector de personajes?!"),
        ("cheeky", "Buen chiste, 'Monika'... ahora dime que no estas escribiendo python para borrarnos verdad?!"),
    ],
    "sayori": [
        ("surprised", "EHHH?! Te llamas MONIKA?! Pero si Monika estaba ahi al ladito hace un segundo!!"),
        ("thinking", "Si hay DOS Monikas... significa que ahora me tocan el DOBLE de galletas o doble de tarea del club?!"),
        ("blush", "Uwaa... no me asustes con clonaciones raras! Mi cerebro chiquito se hace nudo!"),
        ("laugh", "Hola Monika numero dos!! No le borres la carpeta de cupcakes a Natsuki por favor!"),
    ],
    "yuri": [
        ("surprised", "M-Monika...?! Por que siento un escalofrio metafisico en toda la linea de tiempo de este archivo?!"),
        ("thinking", "Dos entidades bajo el nombre 'Monika'... esto viola las reglas narrativas mas basicas del club."),
        ("shy", "P-perdona 'Monika', cuando alguien se pone ese nombre siento que las letras del chat parpadean solas..."),
        ("sad", "Esperemos que ninguna 'Monika' decida alterar los parametros de personalidad esta noche..."),
    ],
}

MONIKA_NAME_WEIRD_EN: Dict[str, DialogueList] = {
    "monika": [
        ("surprised", "WAIT... WHY IS YOUR NAME MONIKA?! I AM MONIKA!! Who touched characters/monika.chr?!"),
        ("laugh", "Two Monikas in one Wayland session... ahaha, either you are my mirror clone or a config glitch!"),
        ("cheeky", "There can only be one club president, 'Monika'... good thing I like myself!"),
        ("thinking", "Monika talking to 'Monika' watching Monika. We just broke the fourth wall twice."),
    ],
    "natsuki": [
        ("surprised", "WHAT?! Your name is MONIKA?! One file-meddling club president was plenty already!!"),
        ("angry", "Do NOT start deleting my manga stash just because you named yourself 'Monika'!!"),
    ],
    "sayori": [
        ("surprised", "EHHH?! Wait, YOU are Monika?! Then who is the other Monika in the outfit drawer?!"),
        ("happy", "Two Monikas?! Does that mean double club snacks or double club work?!"),
    ],
    "yuri": [
        ("surprised", "M-Monika...?! Why do I feel a sudden cosmic disturbance inside this JSON config...?"),
        ("shy", "Please do not rewrite anyone's character variables tonight, 'Monika'..."),
    ],
}

SWARM_REPLIES_ES: Dict[str, Tuple[str, str]] = {
    "natsuki": ("blush", "Q-QUE DIJISTE?! B-baka! Mira a todas volando felices por tu culpa!!"),
    "monika": ("excited", "Ahaa! Escucharon eso chicas?! Todo el club salio volando de pura felicidad!!"),
    "sayori": ("excited", "WIIIIII!! NOSOTRAS TAMBIEN TE AMAMOS MUCHO!! MIRA COMO VOLAMOS!!"),
    "yuri": ("blush", "A-ah!! Nos hiciste alzar el vuelo a las cuatro... q-que emocion tan hermosa!!"),
}

SWARM_REPLIES_EN: Dict[str, Tuple[str, str]] = {
    "natsuki": ("blush", "W-WHAT DID YOU SAY?! D-dummy!! Look at all of us flying because of you!!"),
    "monika": ("excited", "Ahaha! Did you hear that everyone?! The whole club is flying with joy!!"),
    "sayori": ("excited", "WHEEEEE!! WE LOVE YOU TOO!! LOOK AT ALL FOUR OF US FLYING!!"),
    "yuri": ("blush", "A-ah!! All four of us took flight at once... h-how wonderfully touching!!"),
}

