---
marp: true
paginate: true
---


<style>
    /* @theme blackwire */

/* ============================================================
   blackwire — a Marp theme for security/hacking talks
   Clean black background, red→purple accents, mono details.

   Usage:
     marp deck.md --theme blackwire.css
   or in the deck front-matter:
     ---
     marp: true
     theme: blackwire
     paginate: true
     ---

   Slide classes:
     <!-- _class: lead -->      title / section-opener slide
     <!-- _class: divider -->   full-bleed gradient section break
     <!-- _class: invert -->    surface (lighter) background panel
   ============================================================ */

@import url('https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;700&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500;700&display=swap');

:root {
  --bg:        #0a0a0c;
  --surface:   #141419;
  --line:      #26262e;
  --text:      #e9e9ee;
  --muted:     #8a8a96;
  --red:       #ff3b5c;
  --purple:    #a855f7;
  --grad:      linear-gradient(90deg, var(--red) 0%, var(--purple) 100%);

  --font-display: 'Space Grotesk', system-ui, sans-serif;
  --font-body:    'Inter', system-ui, sans-serif;
  --font-mono:    'JetBrains Mono', ui-monospace, 'SF Mono', Menlo, monospace;
}

/* ---------- base slide ---------- */
section {
  width: 1280px;
  height: 720px;
  padding: 80px 96px 72px;
  background-color: var(--bg);
  /* faint dot grid — keep it subtle so it never fights the content */
  background-image: radial-gradient(circle, #1a1a22 1px, transparent 1px);
  background-size: 32px 32px;
  color: var(--text);
  font-family: var(--font-body);
  font-size: 26px;
  line-height: 1.5;
  letter-spacing: 0.01em;
  position: relative;
  overflow: hidden;
}

/* gradient hairline across the top of every slide — the signature */
section::before {
  content: '';
  position: absolute;
  top: 0; left: 0; right: 0;
  height: 4px;
  background: var(--grad);
}

/* ---------- headings ---------- */
h1, h2, h3, h4 {
  font-family: var(--font-display);
  font-weight: 700;
  line-height: 1.1;
  letter-spacing: -0.01em;
  margin: 0 0 0.4em;
}

h1 {
  font-size: 64px;
  color: var(--text);
}

/* gradient rule under top-level headings */
h1::after {
  content: '';
  display: block;
  width: 96px;
  height: 4px;
  margin-top: 0.35em;
  background: var(--grad);
  border-radius: 2px;
}

h2 {
  font-size: 44px;
  color: var(--text);
}

/* terminal-style marker on h2 */
h2::before {
  content: '> ';
  font-family: var(--font-mono);
  color: var(--red);
  font-weight: 500;
}

h3 {
  font-size: 32px;
  color: var(--purple);
  font-weight: 500;
}

h4 {
  font-size: 26px;
  color: var(--muted);
  font-family: var(--font-mono);
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

/* ---------- text ---------- */
p { margin: 0 0 0.7em; }

strong { color: var(--red); font-weight: 600; }
em     { color: var(--purple); font-style: normal; }

a {
  color: var(--purple);
  text-decoration: none;
  border-bottom: 1px solid rgba(168, 85, 247, 0.4);
}
a:hover { color: var(--red); border-color: var(--red); }

mark {
  background: rgba(255, 59, 92, 0.18);
  color: var(--text);
  padding: 0 0.2em;
  border-radius: 3px;
}

/* ---------- lists ---------- */
ul, ol { margin: 0 0 0.7em; padding-left: 1.4em; }
li { margin: 0.3em 0; }

ul > li::marker { color: var(--red); }
ol > li::marker { color: var(--purple); font-family: var(--font-mono); }

/* ---------- code ---------- */
code {
  font-family: var(--font-mono);
  font-size: 0.86em;
  background: var(--surface);
  color: var(--red);
  padding: 0.12em 0.4em;
  border-radius: 4px;
  border: 1px solid var(--line);
}

pre {
  background: var(--surface);
  border: 1px solid var(--line);
  border-left: 3px solid var(--purple);
  border-radius: 8px;
  padding: 24px 28px;
  margin: 0.6em 0;
  font-size: 22px;
  line-height: 1.45;
  overflow: auto;
  color: var(--text);
}
pre code {
  background: none;
  border: none;
  padding: 0;
  color: var(--text);
  font-size: 1em;
}

/* highlight.js token tuning toward the palette */
.hljs-keyword, .hljs-built_in, .hljs-literal { color: var(--purple); }
.hljs-string, .hljs-attr, .hljs-symbol       { color: #ff7a93; }
.hljs-title, .hljs-function, .hljs-class      { color: var(--red); }
.hljs-number, .hljs-meta                      { color: #c084fc; }
.hljs-comment, .hljs-quote                    { color: var(--muted); font-style: italic; }

/* ---------- blockquote ---------- */
blockquote {
  margin: 0.6em 0;
  padding: 0.2em 0 0.2em 28px;
  border-left: 3px solid var(--red);
  color: var(--muted);
  font-style: italic;
}
blockquote p:last-child { margin-bottom: 0; }

/* ---------- tables ---------- */
table {
  width: 100%;
  border-collapse: collapse;
  font-size: 22px;
  margin: 0.6em 0;
}
th {
  font-family: var(--font-mono);
  text-transform: uppercase;
  letter-spacing: 0.06em;
  font-size: 0.8em;
  text-align: left;
  color: var(--purple);
  padding: 12px 16px;
  border-bottom: 2px solid var(--line);
}
td {
  padding: 12px 16px;
  border-bottom: 1px solid var(--line);
}
tr:last-child td { border-bottom: none; }

/* ---------- images ---------- */
img {
  border-radius: 8px;
}

/* ---------- pagination / header / footer ---------- */
section::after {
  font-family: var(--font-mono);
  font-size: 18px;
  color: var(--muted);
  right: 96px;
  bottom: 40px;
}

header {
  font-family: var(--font-mono);
  font-size: 16px;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  color: var(--muted);
  top: 36px;
  left: 96px;
}

footer {
  font-family: var(--font-mono);
  font-size: 16px;
  color: var(--muted);
  left: 96px;
  bottom: 40px;
}

/* ============================================================
   LEAD — title slide / section opener
   ============================================================ */
section.lead {
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: flex-start;
  padding: 96px 120px;
}
section.lead h1 {
  font-size: 92px;
  line-height: 1.05;
}
section.lead h1::after { width: 160px; height: 6px; }
section.lead h2 {
  color: var(--muted);
  font-family: var(--font-mono);
  font-weight: 400;
  font-size: 30px;
  margin-top: 0.6em;
}
section.lead h2::before { content: none; }
section.lead p {
  font-family: var(--font-mono);
  color: var(--muted);
  font-size: 22px;
}

/* ============================================================
   DIVIDER — full-bleed gradient section break
   ============================================================ */
section.divider {
  background: var(--grad);
  background-image:
    radial-gradient(circle, rgba(0,0,0,0.18) 1px, transparent 1px),
    var(--grad);
  background-size: 32px 32px, 100% 100%;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: flex-start;
  padding: 96px 120px;
}
section.divider::before { background: rgba(0,0,0,0.35); }
section.divider h1,
section.divider h2,
section.divider h3 { color: #0a0a0c; }
section.divider h1::after { background: #0a0a0c; }
section.divider h2::before { color: #0a0a0c; }
section.divider::after { color: rgba(0,0,0,0.55); }

/* ============================================================
   INVERT — surface panel for emphasis slides
   ============================================================ */
section.invert {
  background-color: var(--surface);
  background-image: radial-gradient(circle, #20202a 1px, transparent 1px);
}
</style>


# Turning a game console into a hacking device


---

# About the speaker

<pre>
$> whoami

Jeremie Amsellem (Lp1)

------

Hacker,

Reformed mobile/web/IoT developer

PenTester // Information Security Trainer

Founder @Fenrir.pro
</pre>


---

# About this talk (TL;DR)

- In summer 2024, I started writing PS Vita homebrews
- After my first "hello, world"s, I thought that it would be neat to create a framework that would allow creation of homebrews in HTML/JS
- In 2025, as a proof of concept I wrote a hosts/ports scanner for the console using my new framework **quark**
- Today I'm here to present **VipperZero**, a new project turning the console into a hacking device


---

# About the project

<!-- 

It's an idea that came to me after the ban of the FlipperZero in Canada.

The flipper zero is a hacking device that looks like a gaming device.

Wouldn't it be fun to turn a genuine gaming device into a hacking device?

- Plenty to learn for low-level networking RF, BT etc...

+ I have the PERFECT HANDHELD for this: The PS Vita

- Lightweight
- Small
- Big screen
- Tactile
- BT/WiFi Chipset
- Already Jailbroken in MANY ways

 -->



![bg right 110%](./images/vipper1.png)

---

# PS Vita Hacking (1/2)

<br/>
<br/>
<br/>
<br/>
<br/>
<br/>
<br/>
<br/>
<br/>
<br/>

<!-- What is considered "hacking" a console?

Usually the objective is to run unofficial software, also called "Homebrews". 

You probably know that If you get in any way arbitrary code execution on a device, 
usually someone will port DOOM to it a few days later.

But the goal is hard to achieve, these devices have only been designed to run signed, official game cartridges or downloaded game packages AND often run their software in 
sandboxed environment.

 -->


<!-- Nowadays, the PS Vita is one of the MOST hacked mobile device, there's thousands of amazing homebrews to add countless features to the console.-->

<!-- TODO: Illustation homebrews -->

![bg](./images/homebrews.png)

---

# PS Vita Hacking (2/2)

<!-- When it came out in 2012, the ps vita was considered one of the thoughest mobile devices to hack. Its environment has a good separation between what the kernel can do and what the user(s) can do and  

Since it runs a full PSP environment (you can download PSP games on the store and run them, just like normal PS Vita games). They run in a sandbox and since the PSP was sony's most hacked device, hackers started searching on the PSP side of the vita.

The first hacks came from vulnerabilities in games allowing code execution, hackers found them, sony removed the game from the store, in a loop until 2014.

 -->

<!-- TODO: Reference https://wololo.net/2022/07/25/vita-hacking-history/ -->

![bg right](./images/vita_hack2.jpg)


---


## The big hack

<!-- In september 2014 something happened

A hacker known as qwikrazor87 disclosed an exploit chain (using the game Gladiator begins) that gave him kernel access to the vita's PSP sandbox.

It wasn't the first discovered, it was released it in a hurry because another hacker from his team threatened to leak it.

And a few days later... he did. Acid_Snake, leaked 50 of qwikrazor's exploits ("corruped" game saves).

In terms of quantity, that's probably the biggest exploit leak in the history of playstation hacking.

And for many people it was a huge waste: If people (and Sony) knew about these 50 exploits at the same time, they would be able to patch them all at once, reinforce the security of the console. 

But on the bright side, it forced developers to stop targeting the PSP sandbox and work directly on the vita's firmware itself.

--->



<!-- //TODO Cap done, illustration Acid_Snake -->

![alt text](images/tweet.png)


---

## 2015 - A new hope

<!-- 

Early 2015, someone (Hykem) finds an exploit in the way WebKit (that the PS Vita uses) handles URIs.

Some URIs can be used to start existing apps on the console.
 -->

<!-- 

WebKit is a Web Browsing engine, used by safari and multiple consoles.

It has also  been used for jailbreaking the PS3 Wii and Wii U (among others)

-->

<!-- Illustration webkit -->


<!-- But in June, something more impactful happened: the hacker Yifan Lu created Rejuvenate, the first native exploit for the PS Vita allowing code execution -->

![alt text](./images/rejuvenate.png)

<!-- It totally recreated hope and enthusiasm in the Vita Hacking scene
-->

--- 

## The birth of VitaSDK

![bg right 90%](./images/vitasdk.png)

---

## Timeline

- **September 2015**: Arbitrary file write via email attachments
- **May 2015**: VitaSDK is created
- **July 2016**: Henkaku is released
- **July 2017**: Enso is released
- **July 2019**: The PS Vita's encryption is defeated
- **January 2021** : Android ports

<!-- 2016 - Remember the webkit exploit? Henkaku allows hacking your console by browsing a website. But the hack is temporary, if you reboot your console, it's gone. 
Also it requires to have a developer licence because it requires using Sony's official SDK.
-->

<!-- Enso allows instaling a custom firmware, and 2017 is also the year the hacker TheFlow found a way to bypass Sony's DRM  -->


<!-- 2019 - The hacker xyz, without believing it too much bruteforced the key stored in the processor that handles most of encryption tasks on the console.

It was AAAAAAAAAAAAAAAA (16xA)

 -->

 <!-- The story says that hacker TheFlow, semi-drunk and wating to play San Andreas on his vita, created a .so loader a night allowing to boot Android games -->

 <!-- They do require a bit of portage work, but it works! -->

<!-- OpenGL Support, unofficial SDK -->

--- 

## Piracy

<!-- I told you that TheFlow found a way to completly bypass Sony's DRM. Well more importantly, someone else noticed that the PS vita was downloading packages from Sony's server directly when downloading a game, without verifying of the device/user actually owns the game. 

The console is the one that actually make this verification. But with the DRM broken... -->

<!-- Long story short, you can just download a game package on Sony's servers and run it on your console. Thanks Sony! -->

<!-- As of today (I guess because it can't really fixed without re-designing the whole package download system on the console), it still has not been fixed by sony.-->

<!-- //TODO screenshot pkgj 

https://github.com/blastrock/pkgj

-->
![](./images/pkgj.png)

---

# Writing software for the PS Vita in 2026

<!-- //TODO illustration vitaSDK -->

<!-- Nowadays we are lucky enough to have a complete PS vita SDK that even though it's unofficial is very complete -->

<!--  -->

<!-- //TODO screen wiki + samples vitaSDK -->

<!-- We only have this thanks to people reverse engineering the ps Vita's firmware. -->


---

## Hello, world

<!-- At first, I created simple apps using a graphics lib that I like: SDL.

I was happy.

But after writing a few hundred lines of kind of redundant C code, I wondered if I couldn't make my life _easier_//10 times harder

That's when things went wrong.

What if... Instead of building my interfaces in SDL by creating structures in memory for my squares, fonts, colors.

I had a tool to building them in HTML/CSS? It would also be cool to be able to dynamically add logic with JavaScript code for instance, really, how hard could it be??

 -->


![bg right](./images/helloworld.png)

---

## Horrendous Text Markup Language 

<!-- If you're thinking that a good knowledge of network protocols and of the HTML standards are enough to make a HTML renderer without going insane. You're probably wrong. -->

<!-- Just supporting the body, header and div tags with a bit of CSS is geometry and parsing hell.-->

<!--  -->

![bg right 100%](./images/output.gif)

---

# What I've built

## Quark

<!-- That's why I've based the code of my framework quark on an existing HTML renderer -->

- Lexbor (HTML)
- Duktape (JS)
- SDL 


<!-- 
Lexbor that is used by PHP on the latest versions.
 -->


<!-- Same for the JS, building a JS engine is a whole new project by itself so I'm using duktape that is a JS engine built for devices with not a lot of memory, so perfect for our use case-->

<!-- 

And since I'm using the SDL that is multi-platform;

With quark I can run and design complex interfaces and test them directly on my Linux computer.

-->

<!-- //TODO prepare simple demo where I run a sample (with not a lot of files in the dir to understand how it works) app on quark in the VM -->

<!-- Show some code too -->

<!-- After building a few demos I was happy with -->

---

## Vita Ports Scanner

<!-- //TODO screen -->


<!-- I wanted to learn more about the low-level I/O capabilities of the console; well I was into the hard subjects right away!

Timing issues, locked threads, random crashes.

It's network dev but without the documentation. 

 -->

<!-- 
 
//TODO Show screen with the results from ddg (3 results)

 -->


<!-- 3720 downloads on the unofficial store VitaDB -->

<!-- //TODO show somewhere what it looks like when an app crashes on the vita -->


![](./images/screenshot.gif)

---

## Vita Keyboard

![](./images/vitakeyboard.png)

---

## Vipper Zero

<!-- Now that I had these pieces together, I know what my next goal was: I was ready to build my Flipper Zero clone and try to fit as much as the features the flipper has in the console -->

<!-- I even had started forking on the UI, which might be slightly inspired from something. -->

![](./images/vipper1.png)

---

### Ideas

- I know how to send keyboard inputs, now I just need to parse duckyscripts for BadUSB functionality
- The PS Vita already has a Bluetooth chipset, I should be able to use the corresponding APIs
- Maybe I can use an external device plugged into the vita for the SubGHz features?

<!-- And speaking of external device, I thought of the EvilCrowRFV2 that I've been using to scan/read/replay radio data in the past -->

---

### EvilCrowRF-V2

![bg right 180%](./images/ecrf.webp)

<!-- 1 esp32 -->
<!-- 2 CC1101 radio modules -->
<!-- 1 NRF24L01 module for the 2.4GHz  -->

---


### Problems

- The bluetooth chipset is OLD...
- We don't have low level access to it anywhere in the SDK
- The EvilCrowRF cannot be used via USB

<!-- That is actually a problem because I really wanted the ECRF to be connected to the vita, a bit like the hats on the Flipper Zero -->

<!-- And yes, I could just have connected to the ECRF via WiFi but 1: I think that's less cool, that's less stealthy (we're exposing a network) and if you still think I like to make my life easier, you clearly didn't follow -->

---

### Solutions

- Rewrite a firmware for the ECRFv2 to communicate with it using USB Serial
- Use the NRF24L01 (2.4GHz) module on the Evilcrow for the Bluetooth exploits

<!-- 2.4GHz: that's the frequency used by Bluetooth Low Energy -->

---

### More problems
- The PS Vita does **not** provide any output voltage and cannot act as a USB host (AFAIK)

<!-- We cannot connect external devices via the PS Vita USB, the ps vita IS THE DEVICE supposed to be connected to a computer -->

- The firmware I built for the ECRFv2 does not make it a USB host either and I don't know how if it's even possible to make it a USB host


<!-- I discovered that the PS Vita CAN act as a USB client and send USB serial data though. -->

<!-- It's like having two usb keyboards and somehow wanting to make them communicate...
 -->

<!-- What if I plugged the ps vita and the ECRF to a host that passes data from one to the other?

 -->

<!-- You might think it's stupid: IT IS, but also it works -->

---

![alt text](./images/setup_pi.jpg)

---


![bg](./images/github_2.png)

---

![bg](./images/wiki.png)


<!-- Cherry on the cake: the PS Vita does not send a valid serial USB header, it causes a crashe of the USB stack on my machine when I plug it in in serial mode -->


---

# Future plans for the project

- Reach a fully stable version 1.0
- Design/3d print a case for the components
- Implement the ECRF's whole feature set
- Support new devices (Nintendo Switch?) 


---

# We need you!

---


# Thanks!

- You
- wololo.net
- //TODO list people in the PS Vita community

# Links

https://github.com/lp1dev/quark
https://github.com/lp1dev/VipperZero

https://lp1.eu