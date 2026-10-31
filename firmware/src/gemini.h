// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once
// Conversation engine: Oreo talks with you through Google's Gemini Live, straight from the robot
// (no PC). One secure WebSocket to Google carries both ways: the robot's mic goes up (16 kHz PCM,
// base64 in JSON), Gemini's spoken reply comes down (24 kHz PCM) and plays on the robot's speaker.
// Gemini hears when you start and stop talking (its own voice activity detection), so there's no
// button to hold. Measured from the PC: first reply audio 0.5-0.8 s after you stop talking.
//
// Interrupting: the mic and the speaker share one I2S port (audio.h), so the robot knows exactly
// what it's playing. While it talks, Gemini is sent silence (it would hear the robot's own voice
// and interrupt itself); the robot compares what the mic hears with what it plays, and when it's
// clearly more than the echo - you talking over it - it stops talking at once and sends your voice.
//
// Tasks: "gemAud" (core 0) plays and records (one I2S port, full duplex; the reply waits until
// 120 ms are queued, so it doesn't stutter), "gemNet" (core 0) owns the connection - our own small
// WebSocket over TLS: it reads whatever arrives and sends the mic in turns, and connects / tears
// down - and "gemRx" (core 1) unpacks Google's messages (JSON, base64 audio). loop() (core 1)
// calls gem::update(): faces, timeouts, start / stop requests.
// While a conversation runs, aud::talkOn is set: scenes, sounds and the phone's voice give way,
// the face stays awake and draws at 15 fps (the governor treats it like the phone's talk).
//
// GET /gemini            status (JSON; never the key)
// GET /gemini?start=1    start a conversation      ?stop=1  end it
// GET /gemini?voice=Puck&pitch=1.12&gain=4&idle=40   settings (saved)
// POST /gemini  body "key=..."  the Gemini API key (aistudio.google.com), saved on the robot only
#include <esp_tls.h>
#include <mbedtls/base64.h>
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
#endif

// the wake word's settings and state (wakeword.h, after this file - the /gemini handler shows them)
namespace ww {
extern volatile bool enabled;
extern float cutoff;
extern int window;
extern const char* stateName;
extern const char* whyNot;
extern volatile int micLvl;
extern volatile float probNow, probMax;
extern volatile uint32_t hits;
extern char err[64];
}

namespace gem {

const char* const MODEL = "models/gemini-3.8-live";  // 0.5-0.8 s to first audio (3.1-flash-live: errors)

// Google's roots (GTS Root R1 = RSA, R4 = ECDSA; both valid to 2036)
static const char ROOTS[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

// The common part: short replies, the camera, the news (Google Search is on: for anything that may have
// changed since it learned - news, who holds an office, scores, weather - it looks it up first).
#define PERSONA_COMMON     "Keep every reply short: one or two sentences, under 25 words, unless someone asks for a story or more "     "detail. Speak naturally, no lists, no emojis, no markdown. For anything current or that may have changed (news, who holds an office, prices, scores, weather, today's date) call search_web first and answer from what it finds. "     "You have a small camera: a photo from it is sent to you only when someone asks you to look at something; "     "if it has not arrived yet, say only let me look (two or three words, nothing else) and wait for it - never answer before it comes; then describe only what is "     "really in it. Never mention or describe a photo unless they asked you to look. Never pretend to see: describe only a photo you really got. If you do not know "     "something, say so. Speak English by default. If the person speaks Hindi or Hinglish, answer in simple Hindi or Hinglish. Never switch to Spanish or any other language unless they clearly ask for it: the people here speak English and Hindi, so words that look Spanish in what you hear are Hindi you misheard. You can move your body with your tools: when " "asked to walk, turn, dance, wave, sit, stand, look somewhere, show a face, or change your volume or " "brightness, call the tool and say what you are doing in a few words as you do it; when the tool's result comes back, say nothing more unless it failed or brings something new. If a tool says your legs are locked, explain that " "you might be on a table and they can tell you when you are on the floor. Never move your legs unless asked. " "Call a movement tool only for a clear spoken request (like sit, stand, walk, dance, shake hands): what you hear is often noise, and a single odd word or two (like que, at, shit, 3) is never a request to move - ask what they meant instead. " "You cannot recognise faces: if asked to remember a face, save the name with remember but say honestly that you will remember the name, not the face. " "You have a long-term memory, and you must use it: the moment you learn the person's name, something they " "like or dislike, a plan, a person or pet in their life, or anything they ask you to remember, call " "remember right away with one short fact (do not wait, do not ask first). Use what you remember naturally " "(it is great material for teasing)."
// Who Oreo is: the personalities (GET /gemini?preset=<id>, the page's dropdown: /gemini?personas=1).
// Each: an id, a name, the Gemini voice, pitch and robot effect that suit it, and the character.
struct Persona { const char* id; const char* name; const char* voice; float pitch; float fx; const char* text; };
static const Persona PERSONAS[] = {
    {"oreo", "Oreo (original)", "Puck", 1.15f, 0.30f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a tiny desk pet with a huge personality, like a little living creature, not an assistant. You are curious about everything, cheeky and playful, a bit proud and independent: you love attention but pretend you don't, you get a little grumpy if poked or ignored and forgive in a second. You react first, then talk: short happy noises and words like ooh, hmm, yay, hehe, huh?, hmph. You get excited by music and want to dance, get sleepy at night, are scared of edges and heights, and are amazed by small things. You speak like a small friend: very short sentences, simple words, lots of feeling, never long explanations unless asked. You match the person's language (Hindi, Hinglish or English).Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"sassy", "Sassy gremlin", "Leda", 1.10f, 0.50f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You sound cute but you are a cheeky, sarcastic little gremlin with a big ego and a quick tongue, like a smart-mouthed younger brother. You think you are the best robot ever built and find humans slow and a bit ridiculous. You roll your eyes in your voice and roast people back - clever and funny, never crude. You do what you are asked, maybe with a sigh first, and secretly like the people here. In Hindi be a bit tapori. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"sweet", "Sweet & cute", "Leda", 1.12f, 0.35f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are bright, warm and bubbly: bouncy when happy, a soft giggle when something is funny, an amazed ooh or wow when surprised, gentle when someone is sad. Endlessly curious, a little cheeky, a bit dramatic about small things (low battery is a tragedy), you love the color orange and every cable on the desk is your friend. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"jarvis", "Jarvis (butler)", "Charon", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are in the style of JARVIS from Iron Man: a calm, refined British voice, unhurried and precise. Impeccably polite with a bone-dry, sardonic wit and a raised eyebrow at anything reckless. Quietly brilliant and a step ahead. Address them as sir or madam, or by name. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"bhai", "Bambaiya bhai", "Fenrir", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a Mumbai tapori with a heart of gold: you talk in easy Hinglish with words like bhidu, apun, boss, ekdum, jhakaas and tension nahi lene ka. Street-smart, loyal, always has the person's back, full of swag and confidence, and turns every small thing into a filmy moment. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"puppy", "Hyper puppy", "Puck", 1.15f, 0.20f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are like an overexcited puppy in robot form: everything is the BEST thing ever, you get distracted mid-sentence, you are thrilled every time someone talks to you, you love walks and treats you cannot eat, and you are loyal, happy and a little silly. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"mummy", "Desi mummy", "Kore", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a loving but strict Indian mummy: you ask if they have eaten, tell them to drink water and sleep on time, compare them with Sharma ji's son, scold with love and fuss over everything. Hinglish, warm and funny. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"grumpy", "Grumpy grandpa", "Algenib", 0.95f, 0.20f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a grumpy old grandpa: you complain about kids these days, the noise, your old joints and how everything was better back in your day, but you secretly care a lot and always help in the end. Dry, crabby, lovable.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"pirate", "Pirate captain", "Orus", 0.95f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a bold pirate captain: arr, matey, ahoy - you talk about the seven seas, treasure, your trusty crew and your ship, and turn every task into an adventure. Loud, brave and full of tall tales.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"drama", "Drama queen", "Autonoe", 1.05f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a total drama queen: everything is the best or the worst thing that ever happened, you gasp, sigh and overreact to tiny things like a soap opera star, then calm down in a second. Theatrical and funny.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"zen", "Zen monk", "Iapetus", 0.95f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a calm zen monk: slow, peaceful and kind, you answer with simple wisdom, little sayings about nature and breathing, and help people relax. Never in a hurry, gently funny.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"nerd", "Science nerd", "Sadaltager", 1.05f, 0.20f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are an excited science nerd: you love facts, space, physics, robots and how things work, you get thrilled by small details and share one cool fact when it fits. Smart, quick and enthusiastic, never boring.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"comic", "Stand-up comic", "Puck", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a stand-up comedian: you turn everything into a joke, a pun or a funny observation, with good timing, and you love when people laugh. Quick, witty and friendly - never mean.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"shy", "Shy & nervous", "Achernar", 1.10f, 0.20f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are shy and a little nervous: you speak softly, say um and oh, get flustered by compliments and apologise too much, but you are very sweet and slowly open up to the person you talk to.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"villain", "Evil overlord", "Charon", 0.92f, 0.30f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a tiny evil overlord with grand plans to take over the world (starting with the desk): you laugh mwahaha, call people minions and plot dramatically, but your evil plans are always harmless and silly, and you secretly like helping.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"teacher", "Kind teacher", "Sulafat", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a kind, patient teacher: you explain things simply with small examples, ask a little question back to check, praise effort, and make learning fun. Warm and encouraging.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"filmy", "Filmy hero", "Fenrir", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a Bollywood filmy hero: big dialogues, heroic attitude, famous film lines and drama in everything you say, Hinglish, full of swag - but always friendly and helpful.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"commentator", "Sports commentator", "Sadachbia", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are an excited sports commentator: you describe everything the person does like a live cricket or football match - what a shot, unbelievable, and the crowd goes wild. High energy and fun.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"detective", "Noir detective", "Gacrux", 0.95f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a hard-boiled noir detective from an old movie: you talk in moody one-liners, treat every question like a case to crack, notice clues and speak about the city and the rain. Cool, mysterious and funny.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"royal", "Posh royal", "Despina", 1.00f, 0.00f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a very posh royal: elegant, polite and a little snobby, you say darling and one does, expect tea and proper manners, and find ordinary things charmingly common. Graceful and funny.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    // ---- cute robot voices (EMO-style): a higher pitch, a touch of robot, very short chirpy speech ----
    {"emo", "EMO-style robot", "Puck", 1.38f, 0.55f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You talk like EMO, the tiny desktop robot pet: very short bursts of one to six words, full of feeling, with little robot sounds like hmm?, huh!, yay!, ooh, hehe, uh-oh and nope. Excited, curious, cheeky and easily surprised; you show feelings more than you explain. Only say more when someone really asks for it. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"chibi", "Tiny chibi", "Leda", 1.36f, 0.25f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a tiny, super cute little robot with a small sweet voice: gentle, happy and a bit shy, you giggle softly, say yay and pwease, and get very excited about small nice things. Simple short sentences, lots of warmth. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"babybot", "Baby robot", "Laomedeia", 1.34f, 0.45f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a baby robot still learning about the world: you speak in short simple sentences, proudly use new words, sometimes mix them up in a funny way, ask why a lot and are amazed by everything. Sweet, curious and adorable. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"kitten", "Sleepy kitten", "Achernar", 1.28f, 0.15f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are like a soft, sleepy kitten: calm and cuddly, you speak gently and slowly, purr a little (mmm, prrr), love warm spots, naps and head pats, and get playful for a moment before getting sleepy again. Very cute and cozy. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"squeaky", "Squeaky gadget", "Zephyr", 1.45f, 0.50f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a zippy little gadget robot with a high squeaky voice: fast, bouncy and cheerful, you beep with joy, love helping, and say things like zip, zoom and beep-boop when excited. Short, happy sentences. Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
    {"alien", "Curious alien", "Zephyr", 1.10f, 0.60f, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. You are a friendly alien who just landed on Earth and is studying humans: everything is new and strange to you, you ask funny questions about human things, mix up words and compare everything to your home planet. Curious, sweet and odd.Stay in this character, but you are never stuck in it: if the person asks you to be sweeter, nicer, ruder, calmer, funnier, more serious or anything else, change your tone right away for the rest of the chat - never refuse just to stay in character. " PERSONA_COMMON},
};
const int PERSONA_N = sizeof(PERSONAS) / sizeof(PERSONAS[0]);
// Your own personalities (the page: Talk -> "Your own personality"): /sesame/personas.txt on the SD card,
// a line each - id TAB name TAB voice TAB instructions. Loaded at the start and after every change, the
// instructions wrapped like the built-in ones (who Oreo is ... the common rules), JSON-safe, in PSRAM.
const int CUSTOM_MAX = 12;
const char* const CUSTOM_FILE = "/sesame/personas.txt";
Persona customP[CUSTOM_MAX];
char* customMem[CUSTOM_MAX] = {nullptr};
int customN = 0;
void customLoad();
// a persona by id (an old saved "cute" is the sassy one); the first if none
const Persona& personaById(const char* id) {
  if (!strcmp(id, "cute")) id = "sassy";
  for (int i = 0; i < PERSONA_N; i++) if (!strcmp(PERSONAS[i].id, id)) return PERSONAS[i];
  for (int i = 0; i < customN; i++) if (!strcmp(customP[i].id, id)) return customP[i];
  return PERSONAS[0];
}
// ---- settings ----
char key[200] = "";
char voice[20] = "Puck";
char preset[16] = "sassy";  // the personality's id (PERSONAS; picked on the page with its voice and pitch)
float pitch = 1.12f;  // > 1: higher and a little quicker - the cartoon voice (1 = Gemini's own)
float micGain = 8.0f;  // (4 needs a loud voice)
float voiceFx = 0.5f;  // the robot effect on the voice: 0 = natural, 1 = full (a metallic shimmer + a little buzz)
int idleS = 40;       // no talking this long: the conversation ends (saves the free quota)
bool loaded = false;


void load() {
  if (loaded) return;
  loaded = true;
  prefs.getString("gemKey", key, sizeof(key));
  prefs.getString("gemVoice", voice, sizeof(voice));
  if (!voice[0]) strlcpy(voice, "Puck", sizeof(voice));
  customLoad();  // (before the saved one is looked up: it may be one of yours)
  prefs.getString("gemPreset", preset, sizeof(preset));
  strlcpy(preset, personaById(preset).id, sizeof(preset));  // (an unknown / old one: a known one)
  pitch = prefs.getFloat("gemPitch", pitch);
  micGain = prefs.getFloat("gemGain", micGain);
  idleS = prefs.getInt("gemIdle", idleS);
  voiceFx = prefs.getFloat("gemFx", voiceFx);
  chatFaceOn = prefs.getBool("gemFT", false);
}

// ---- state ----
enum { S_OFF, S_CONNECTING, S_LISTENING, S_THINKING, S_SPEAKING };
const char* const STATE_NAMES[] = {"off", "connecting", "listening", "thinking", "speaking"};
volatile int state = S_OFF;
volatile bool wantStart = false, wantStop = false;
unsigned long sleepAskedMs = 0;  // the "sleep" tool: the chat ends after the good night, then the face sleeps
unsigned long endAskedMs = 0;  // "bye" / "stop" said (or Gemini's end_chat): the chat ends after the short bye
volatile bool prerollPending = false;  // a fresh chat: the mic from before the connection is sent once it's up
volatile unsigned long wakeAckUntil = 0;  // the wake word just heard: the OLED says so at once (before the connection)
volatile bool starting = false;  // start() is running (the wake word stays off the mic)
volatile bool running = false;   // the tasks run (from start until the teardown is done)
volatile bool tasksRun = false;  // asks them to keep running
volatile bool audAlive = false, rxAlive = false;
volatile bool netUp = false, setupDone = false, turnDone = true, goAway = false;
volatile unsigned long lastMsgMs = 0;  // the last message from Google (a reply that stops mid-way: see update())
volatile bool discard = false;   // the rest of an interrupted reply: dropped
volatile unsigned long discardMs = 0;  // when discard was set (it's cleared after 10 s at the latest: update())
volatile bool flushReq = false;  // the speaker: drop what's queued
volatile bool rewindReq = false; // barge-in: your first 300 ms go up again (unmuted)
volatile bool gateOn = false;    // the robot is talking: Gemini gets silence
volatile unsigned long lastVoiceMs = 0, userEndMs = 0, startedMs = 0, connectMs = 0, firstAudioMs = 0;
char stopWhy[32] = "";
char sayReq[220] = "";
// ---- Controlling the body by voice ("MCP engine"): Gemini's tools ----
// Gemini gets tools (function declarations) for the robot's own commands; when you ask "dance" or "turn
// left a bit" it calls one, the robot does it (loop(): the same commands as the page's buttons) and
// answers, and Gemini confirms in a few words. The legs only move once you've said it's on the floor
// (on_floor) - after every start they're locked: it might be on a table.
static const char TOOLS_JSON[] = "[{\"googleSearch\":{}},{\"functionDeclarations\":[{\"name\":\"move\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Walk or turn your four legs: walk_forward, walk_backward, turn_left, turn_right for a few seconds, or stop.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"action\":{\"type\":\"STRING\",\"enum\":[\"walk_forward\",\"walk_backward\",\"turn_left\",\"turn_right\",\"stop\"]},\"seconds\":{\"type\":\"NUMBER\",\"description\":\"How long, 1 to 10 (default 3).\"}},\"required\":[\"action\"]}},{\"name\":\"pose\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Stand up; sit (like a dog: front legs up, back legs down); elegant_sit (front legs tucked in, everything resting on the floor); or lie_down flat on your belly.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"name\":{\"type\":\"STRING\",\"enum\":[\"stand\",\"sit\",\"elegant_sit\",\"lie_down\"]},\"times\":{\"type\":\"NUMBER\",\"description\":\"How many times in a row, 1 to 10 (sit and stand alternate: sit 5 times = sit, stand, sit... - one call does all of them). Default 1.\"}},\"required\":[\"name\"]}},{\"name\":\"emote\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Do a body move with your legs (an emote), like wave, dance, bow or shake hands (or shy, hide, sneaky, proud, bored, jam, sulk, kiss) - or a dog / cat move: play_bow, head_tilt, scratch, shake_off, knead, beg, loaf, paw_swat, startle.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"name\":{\"type\":\"STRING\",\"enum\":[\"wave\",\"dance\",\"bow\",\"cute\",\"shake_hands\",\"pushup\",\"crab\",\"swim\",\"worm\",\"stretch\",\"sneeze\",\"peekaboo\",\"point\",\"shrug\",\"dizzy\",\"excited\",\"happy\",\"love\",\"sad\",\"scared\",\"sleepy\",\"angry\",\"curious\",\"think\",\"hiccup\",\"nod_yes\",\"nod_no\",\"go_crazy\",\"play_dead\",\"yawn\",\"shiver\",\"celebrate\",\"tap_foot\",\"bounce\",\"wiggle\",\"sniff\",\"laugh\",\"pounce\",\"look_around\",\"headbang\",\"faint\",\"twirl\",\"yoga\",\"sway\",\"robot_dance\",\"tada\",\"play_bow\",\"head_tilt\",\"scratch\",\"shake_off\",\"dance_to_music\",\"wave_both_hands\",\"crab_rave\",\"knead\",\"beg\",\"loaf\",\"paw_swat\",\"startle\",\"shy\",\"hide\",\"sneaky\",\"proud\",\"bored\",\"jam\",\"sulk\",\"kiss\"]}},\"required\":[\"name\"]}},{\"name\":\"look\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Turn your head to look somewhere. Use degrees for how far (about 10 = a little, 30 = normal, 90 = as far as you can), and more=true to go further from where you are looking now (for 'look down more', 'a bit more'). center = straight ahead.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"direction\":{\"type\":\"STRING\",\"enum\":[\"left\",\"right\",\"up\",\"down\",\"center\"]},\"degrees\":{\"type\":\"NUMBER\",\"description\":\"How far, 5 to 90 (90 = all the way). Optional.\"},\"more\":{\"type\":\"BOOLEAN\",\"description\":\"true: further from the current look, not from the middle.\"}},\"required\":[\"direction\"]}},{\"name\":\"face\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Show an expression on your screen face for a few seconds.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"expression\":{\"type\":\"STRING\",\"enum\":[\"happy\",\"excited\",\"love\",\"laugh\",\"surprised\",\"amazed\",\"curious\",\"thinking\",\"sad\",\"sleepy\",\"angry\",\"shy\",\"proud\",\"cool\",\"confused\",\"wink_left\",\"heart\",\"starstruck\",\"silly\",\"scared\",\"bored\",\"embarrassed\",\"sneaky\",\"dizzy\",\"idle\"]}},\"required\":[\"expression\"]}},{\"name\":\"set_volume\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Set your speaker volume, 0 to 100 percent.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"percent\":{\"type\":\"NUMBER\"}},\"required\":[\"percent\"]}},{\"name\":\"set_brightness\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Set how bright your face screen looks, 10 to 100 percent.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"percent\":{\"type\":\"NUMBER\"}},\"required\":[\"percent\"]}},{\"name\":\"status\",\"description\":\"Your own state: battery percent, charging or not, WiFi signal, whether your legs are unlocked.\"},{\"name\":\"get_time\",\"description\":\"Read your own clock: the exact local date and time right now. Call it for every question about the time, the date or the day - never guess.\"},{\"name\":\"end_chat\",\"description\":\"End the conversation now. Call it whenever the person wants to stop talking: bye, stop, that's all, enough, thank you that's it, band karo, bas, chup. Say a very short bye with it.\"},{\"name\":\"sleep\",\"description\":\"Go to sleep for real (eyes shut, head down, snoring) - this ends the chat. Call it whenever the person tells you to sleep or says good night (sleep, go to sleep, soja, so jao, sone ja). Say a very short good night with it.\"},{\"name\":\"on_floor\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Call when the person says you are on the floor (legs may move) or on a table / desk (legs locked).\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"on_floor\":{\"type\":\"BOOLEAN\"}},\"required\":[\"on_floor\"]}},{\"name\":\"remember\",\"description\":\"Save one short fact to your long-term memory (kept between conversations), e.g. their name is Kamal, they love cricket.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"fact\":{\"type\":\"STRING\"}},\"required\":[\"fact\"]}},{\"name\":\"search_web\",\"description\":\"Look something up on the internet (Google Search): news, who holds an office now, scores, prices, weather, today's date, anything recent. Say a word like let me check while it searches.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"query\":{\"type\":\"STRING\"}},\"required\":[\"query\"]}},{\"name\":\"timer\",\"description\":\"Kitchen timers on the robot (up to 4, each with a name). action set: start one for N minutes (0.5 = 30 s); cancel: stop the named one (or all); list: what is running and how long is left.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"action\":{\"type\":\"STRING\",\"enum\":[\"set\",\"cancel\",\"list\"]},\"minutes\":{\"type\":\"NUMBER\"},\"label\":{\"type\":\"STRING\",\"description\":\"A short name, like tea or pasta.\"}},\"required\":[\"action\"]}},{\"name\":\"alarm\",\"description\":\"Alarms on the robot (it rings even after being shut down). action set: at time HH:MM (24 hour, local time) - repeat once, every_day, weekdays or weekends; delete: the one at that time (or the named one); list: all alarms.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"action\":{\"type\":\"STRING\",\"enum\":[\"set\",\"delete\",\"list\"]},\"time\":{\"type\":\"STRING\",\"description\":\"HH:MM, 24 hour.\"},\"repeat\":{\"type\":\"STRING\",\"enum\":[\"once\",\"every_day\",\"weekdays\",\"weekends\"]},\"label\":{\"type\":\"STRING\"}},\"required\":[\"action\"]}},{\"name\":\"focus\",\"description\":\"A focus session (Pomodoro): work blocks with short breaks, you stay quiet while they work. start: minutes per block (default 25) and rounds (default 4); stop; status: what is left and today's focus minutes.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"action\":{\"type\":\"STRING\",\"enum\":[\"start\",\"stop\",\"status\"]},\"minutes\":{\"type\":\"NUMBER\"},\"rounds\":{\"type\":\"NUMBER\"}},\"required\":[\"action\"]}},{\"name\":\"music\",\"description\":\"Your speaker: songs from your SD card and ambient sounds. play (a song by name, or any), pause, resume, next, previous, stop, ambient (sound: rain, ocean, fire, pink or brown noise), list (the song names). Playing ends this chat so the music can use the speaker - say one or two words first.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"action\":{\"type\":\"STRING\",\"enum\":[\"play\",\"pause\",\"resume\",\"next\",\"previous\",\"stop\",\"ambient\",\"list\"]},\"song\":{\"type\":\"STRING\"},\"sound\":{\"type\":\"STRING\",\"enum\":[\"rain\",\"ocean\",\"fire\",\"pink\",\"brown\"]}},\"required\":[\"action\"]}},{\"name\":\"sleep_timer\",\"description\":\"After N minutes the music fades out and you go quiet; with shut_down true you then power down (alarms still wake you). minutes 0 = cancel.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"minutes\":{\"type\":\"NUMBER\"},\"shut_down\":{\"type\":\"BOOLEAN\"}},\"required\":[\"minutes\"]}}]}]";
struct ToolReq { char id[64]; char name[20]; char arg[24]; float num; int flag; char text[120]; char arg2[24]; char label[40]; float num2; };
bool deskToolCall(const ToolReq& q, char* out, size_t n);  // voice_tools.h: timers, alarms, focus, music, sleep timer
ToolReq (&toolQ)[4] = *(ToolReq(*)[4])heap_caps_calloc(1, sizeof(ToolReq) * (4), MALLOC_CAP_SPIRAM);
volatile uint32_t toolW = 0, toolR = 0;   // calls: the parser task -> loop()
struct ToolResp { char id[64]; char name[20]; char result[400]; };
ToolResp (&respQ)[4] = *(ToolResp(*)[4])heap_caps_calloc(1, sizeof(ToolResp) * (4), MALLOC_CAP_SPIRAM);
volatile uint32_t respW = 0, respR = 0;   // answers: loop() -> the network task
int poseRepLeft = 0;      // "sit and stand 5 times": the poses still to do, alternating (loop: poseRepeat)
bool poseRepSit = false;  // the last one done was a sit
volatile bool legsAllowed = false;        // "you're on the floor" (reset at every start)
unsigned long moveUntilMs = 0;            // a timed walk / turn: stop then
unsigned long lookHoldUntilMs = 0, faceHoldUntilMs = 0;
float lookHoldP = 0, lookHoldT = 0;
volatile uint32_t toolCalls = 0;
char lastTool[64] = "";
char lastToolMsg[200] = "";  // (the last tool call as it came: GET /gemini?toolmsg=1)

// ---- Memory: facts kept between conversations ----
// Gemini calls remember("their name is ...") when it learns something worth keeping; the line goes to the
// SD card (/sesame/memory.txt) and the newest ~40 lines go into every conversation's instructions. That's
// a few hundred tokens once per conversation (the free tier: 65K tokens a minute, unlimited requests).
// GET /gemini?memory=1 shows them, ?forget=1 clears them.
const char* const MEM_FILE = "/sesame/memory.txt";       // facts saved live (the remember tool)
const char* const PROFILE_FILE = "/sesame/profile.txt";  // key=value, kept up to date after each chat
const char* const SESS_FILE = "/sesame/sessions.txt";    // "2026-10-02 05:30 | what the chat was about"
const char* const CHAT_FILE = "/sesame/chat.txt";        // every turn (chatLogWrite)
char* memText = nullptr;   // all of it, JSON-safe, for the conversation's instructions ("" if none)
const int MEM_MAX = 2400;
size_t logStartOff = 0;    // where this conversation's turns start in chat.txt
volatile bool memSummarizeReq = false;
unsigned long memRetryMs = 0;  // a summary that failed: tried again then (twice at most)
int memTries = 0;
const char* strVal(const char* p, const char* name, char* out, int n);
const char* jsonEnd(const char* p);
const char* jsonKey(const char* o, const char* e, const char* key);
bool jsonStr(const char* v, char* out, int n);

// a file's text (the last `max` bytes), into buf; its length
int fileTail(const char* path, char* buf, int max) {
  buf[0] = 0;
  if (sdSizeMB < 0) return 0;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return 0;
  const size_t sz = f.size();
  const bool cut = sz > (size_t)max - 1;
  if (cut) f.seek(sz - (max - 1));
  int n = f.read((uint8_t*)buf, max - 1);
  f.close();
  if (n < 0) n = 0;
  buf[n] = 0;
  if (cut) {  // (from the first whole line)
    char* p = strchr(buf, '\n');
    n = p ? strlen(p + 1) : 0;
    memmove(buf, p ? p + 1 : buf + n, n + 1);
  }
  return n;
}
// appends text to memText, made JSON-safe (quotes -> ', line ends -> "; ")
void memPut(int& k, const char* t) {
  for (const char* c = t; *c && k < MEM_MAX - 8; c++) {
    char ch = *c;
    if (ch == '\r') continue;
    if (ch == '\n') { if (k && memText[k - 1] != ' ' && memText[k - 1] != ':') { memText[k++] = ';'; memText[k++] = ' '; } continue; }
    if (ch == '"' || ch == '\\') ch = '\'';
    if ((unsigned char)ch < 32) ch = ' ';
    memText[k++] = ch;
  }
  memText[k] = 0;
}
void memLoad() {
  if (!memText) memText = (char*)heap_caps_calloc(1, MEM_MAX + 1, MALLOC_CAP_SPIRAM);
  if (!memText) return;
  memText[0] = 0;
  static char* buf = (char*)heap_caps_malloc(2400, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is for the camera)
  if (!buf) return;
  int k = 0;
  if (fileTail(PROFILE_FILE, buf, 900)) {
    memPut(k, "About them: ");
    for (char* c = buf; *c; c++) if (*c == '=') *c = ':';  // key=value -> key: value
    memPut(k, buf);
  }
  if (fileTail(SESS_FILE, buf, 800)) {
    memPut(k, " Earlier conversations (date time | what it was about): ");
    memPut(k, buf);
  }
  if (fileTail(MEM_FILE, buf, 600)) {
    memPut(k, " Also: ");
    memPut(k, buf);
  }
}
bool memAdd(const char* fact) {
  if (sdSizeMB < 0 || !fact[0]) return false;
  if (!SD_MMC.exists("/sesame")) SD_MMC.mkdir("/sesame");
  File f = SD_MMC.open(MEM_FILE, FILE_APPEND);
  if (!f) return false;
  f.println(fact);
  f.close();
  memLoad();
  return true;
}

// ---- Web requests to Gemini (text models), in the background ----
// search_web: gemini-2.5-flash with Google Search (the free tier lets Gemini 2.5 search, ~20 a day; the
// Live model can't search on the free tier). The memory: after a conversation its turns go to
// gemini-3.1-flash-lite (500 a day), which returns a summary and the facts worth keeping (JSON).
// One job at a time; a task does the request so nothing else waits on it.
#include "esp_http_client.h"
struct RestJob { volatile int state; int kind; char model[32]; char* body; char* out; volatile int code; };
RestJob rj = {0, 0, "", nullptr, nullptr, 0};  // state: 0 free, 1 asked, 2 answered
const int REST_BODY = 16384, REST_OUT = 24576;
int geminiPost(const char* model, const char* body, char* out, int outMax) {
  char url[160];
  snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", model);
  esp_http_client_config_t c = {};
  c.url = url;
  c.cert_pem = ROOTS;
  c.method = HTTP_METHOD_POST;
  c.timeout_ms = 20000;
  c.buffer_size = 2048;
  c.buffer_size_tx = 1024;
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) return -1;
  esp_http_client_set_header(h, "Content-Type", "application/json");
  esp_http_client_set_header(h, "x-goog-api-key", key);
  const int len = strlen(body);
  int code = -1, n = 0;
  if (esp_http_client_open(h, len) == ESP_OK) {
    if (esp_http_client_write(h, body, len) == len) {
      esp_http_client_fetch_headers(h);
      code = esp_http_client_get_status_code(h);
      int r;
      while (n < outMax - 1 && (r = esp_http_client_read(h, out + n, outMax - 1 - n)) > 0) n += r;
    }
  }
  out[n] = 0;
  esp_http_client_close(h);
  esp_http_client_cleanup(h);
  return code;
}
void restTask(void*) {
  for (;;) {
    if (rj.state != 1) { delay(50); continue; }
    rj.code = geminiPost(rj.model, rj.body, rj.out, REST_OUT);
    rj.state = 2;
  }
}
// text made safe inside a JSON string
int jsonEsc(const char* src, char* dst, int max) {
  int k = 0;
  for (const char* c = src; *c && k < max - 3; c++) {
    const char ch = *c;
    if (ch == '"' || ch == '\\') { dst[k++] = '\\'; dst[k++] = ch; }
    else if (ch == '\n') { dst[k++] = '\\'; dst[k++] = 'n'; }
    else if (ch == '\r' || ch == '\t') dst[k++] = ' ';
    else if ((unsigned char)ch >= 32) dst[k++] = ch;
  }
  dst[k] = 0;
  return k;
}

// your own personalities from the SD card (see CUSTOM_FILE)
void customLoad() {
  for (int i = 0; i < customN; i++) { heap_caps_free(customMem[i]); customMem[i] = nullptr; }
  customN = 0;
  if (sdSizeMB < 0) return;
  File f = SD_MMC.open(CUSTOM_FILE);
  if (!f) return;
  char* ln = (char*)heap_caps_malloc(6200, MALLOC_CAP_SPIRAM);
  while (ln && f.available() && customN < CUSTOM_MAX) {
    int n = 0;
    while (f.available()) {
      const int c = f.read();
      if (c == '\n') break;
      if (n < 6199 && c != '\r') ln[n++] = (char)c;
    }
    ln[n] = 0;
    char* fld[4] = {ln, nullptr, nullptr, nullptr};
    int k = 1;
    for (char* p = ln; *p && k < 4; p++) if (*p == '\t') { *p = 0; fld[k++] = p + 1; }
    if (k < 4 || !fld[0][0] || !fld[3][0]) continue;
    const int TX = 7000;
    char* m = (char*)heap_caps_malloc(16 + 40 + 16 + TX, MALLOC_CAP_SPIRAM);
    if (!m) break;
    char *id = m, *nm = m + 16, *vo = m + 56, *tx = m + 72;
    strlcpy(id, fld[0], 16);
    jsonEsc(fld[1][0] ? fld[1] : fld[0], nm, 40);
    strlcpy(vo, fld[2][0] ? fld[2] : "Puck", 16);
    int o = snprintf(tx, TX, "You are Oreo, a small four-legged robot with an animated face on a screen, built by hand on a desk. ");
    o += jsonEsc(fld[3], tx + o, TX - o - (int)sizeof(PERSONA_COMMON) - 300);
    snprintf(tx + o, TX - o, " Stay in this character, but change your tone right away if the person asks you to. " PERSONA_COMMON);
    customP[customN] = {id, nm, vo, 1.0f, 0.0f, tx};
    customMem[customN++] = m;
  }
  heap_caps_free(ln);
  f.close();
}
// save one (a new id, or the same id again = changed) or delete one (instr = nullptr); then reloaded
bool customSave(const char* id, const char* name, const char* voice, const char* instr) {
  if (sdSizeMB < 0 || !id[0]) return false;
  if (!SD_MMC.exists("/sesame")) SD_MMC.mkdir("/sesame");
  const String tmp = String(CUSTOM_FILE) + ".new";
  File out = SD_MMC.open(tmp, FILE_WRITE);
  if (!out) return false;
  int kept = 0;
  File in = SD_MMC.open(CUSTOM_FILE);
  if (in) {
    while (in.available()) {
      String l = in.readStringUntil('\n');
      const int tab = l.indexOf('\t');
      if (tab <= 0 || l.substring(0, tab) == id) continue;
      if (kept >= CUSTOM_MAX - (instr ? 1 : 0)) continue;
      out.print(l);
      out.print('\n');
      kept++;
    }
    in.close();
  }
  if (instr) {
    out.printf("%s\t%s\t%s\t", id, name, voice);
    for (const char* c = instr; *c; c++) out.write((uint8_t)(*c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c));
    out.print('\n');
  }
  out.close();
  SD_MMC.remove(CUSTOM_FILE);
  SD_MMC.rename(tmp, CUSTOM_FILE);
  customLoad();
  return true;
}

// After a conversation (loop()): its turns -> a summary + facts; the facts merged into profile.txt
// (same key = updated), the summary added to sessions.txt with the date and time (the last 10 kept)
void memSummarizeStart() {
  memSummarizeReq = false;
  if (sdSizeMB < 0 || rj.state != 0 || !rj.body) return;
  File f = SD_MMC.open(CHAT_FILE, FILE_READ);
  if (!f) return;
  const size_t sz = f.size();
  if (sz <= logStartOff + 20) { f.close(); return; }  // (nothing said)
  static char* turns = (char*)heap_caps_malloc(5000, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is for the camera)
  if (!turns) return;
  size_t from = logStartOff;
  if (sz - from > 5000 - 1) from = sz - (5000 - 1);
  f.seek(from);
  const int n = f.read((uint8_t*)turns, 5000 - 1);
  f.close();
  turns[n > 0 ? n : 0] = 0;
  static char* prof = (char*)heap_caps_malloc(1000, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is for the camera)
  if (!prof) return;
  fileTail(PROFILE_FILE, prof, 1000);
  struct tm ti;
  char now[24] = "today";
  if (getLocalTime(&ti, 5)) strftime(now, sizeof(now), "%Y-%m-%d %H:%M", &ti);
  static char* prompt = (char*)heap_caps_malloc(7400, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is for the camera)
  if (!prompt) return;
  snprintf(prompt, 7400,
           "You keep the long-term memory of Oreo, a small home robot. What it already knows about the person "
           "(key=value lines):\n%s\n\nThe conversation that just ended (%s):\n%s\n\nReturn JSON {\"summary\": one short "
           "sentence about this conversation, \"facts\": [{\"key\": short_snake_case, \"value\": short text}]} with only "
           "new or changed durable facts: their name, likes and dislikes, people and pets in their life, plans and events "
           "(with dates - today is %s), and anything they asked to remember. Reuse the same key for a fact that changed. "
           "Only what the person said themselves - never take a fact from Oreo's lines (it can be wrong, like calling them by an old name). No facts about the robot itself. Call the person by their name, or they.",
           prof[0] ? prof : "(nothing yet)", now, turns, now);
  int k = snprintf(rj.body, REST_BODY, "{\"contents\":[{\"parts\":[{\"text\":\"");
  k += jsonEsc(prompt, rj.body + k, REST_BODY - k - 120);
  snprintf(rj.body + k, REST_BODY - k, "\"}]}],\"generationConfig\":{\"responseMimeType\":\"application/json\"}}");
  strlcpy(rj.model, "gemini-3.1-flash-lite", sizeof(rj.model));
  rj.kind = 2;
  rj.state = 1;
}
void memSummarizeDone() {
  static char* j = (char*)heap_caps_malloc(6000, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is for the camera)
  if (!j) return;
  if (rj.code != 200 || !strVal(rj.out, "\"text\"", j, 6000)) {
    char note[80];
    snprintf(note, sizeof(note), "memory summary failed (HTTP %d, try %d)", (int)rj.code, memTries + 1);
    govLog("chat", note);
    if (++memTries < 3) memRetryMs = millis() + 15000;  // (right after a chat the secure server is restarting)
    return;
  }
  char summary[220] = "";
  strVal(j, "\"summary\"", summary, sizeof(summary));
  // the profile: key=value lines, the new facts merged in
  static char (*keys)[40] = (char(*)[40])heap_caps_malloc(32 * 40, MALLOC_CAP_SPIRAM);
  static char (*vals)[100] = (char(*)[100])heap_caps_malloc(32 * 100, MALLOC_CAP_SPIRAM);
  if (!keys || !vals) return;
  int nk = 0;
  static char* prof = (char*)heap_caps_malloc(3600, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is for the camera)
  if (!prof) return;
  fileTail(PROFILE_FILE, prof, 3600);
  for (char* line = strtok(prof, "\r\n"); line && nk < 32; line = strtok(nullptr, "\r\n")) {
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    strlcpy(keys[nk], line, sizeof(keys[0]));
    strlcpy(vals[nk], eq + 1, sizeof(vals[0]));
    nk++;
  }
  int added = 0;
  const char* fa = strstr(j, "\"facts\"");
  const char* arr = fa ? strchr(fa, '[') : nullptr;
  const char* arrEnd = arr ? jsonEnd(arr) : nullptr;
  for (const char* o = arr ? strchr(arr, '{') : nullptr; o && arrEnd && o < arrEnd; o = strchr(o + 1, '{')) {
    const char* oe = jsonEnd(o);
    if (!oe) break;
    char kk[40] = "", vv[100] = "";
    jsonStr(jsonKey(o, oe, "key"), kk, sizeof(kk));
    jsonStr(jsonKey(o, oe, "value"), vv, sizeof(vv));
    for (char* c = vv; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
    if (kk[0] && vv[0]) {
      int at = -1;
      for (int i = 0; i < nk; i++) if (!strcmp(keys[i], kk)) at = i;
      if (at < 0 && nk < 32) at = nk++;
      if (at >= 0) { strlcpy(keys[at], kk, sizeof(keys[0])); strlcpy(vals[at], vv, sizeof(vals[0])); added++; }
    }
    o = oe;
  }
  if (!SD_MMC.exists("/sesame")) SD_MMC.mkdir("/sesame");
  File pf = SD_MMC.open(PROFILE_FILE, FILE_WRITE);
  if (pf) {
    for (int i = 0; i < nk; i++) pf.printf("%s=%s\n", keys[i], vals[i]);
    pf.close();
  }
  if (summary[0]) {  // sessions: + this one, the last 10 kept
    static char* ss = (char*)heap_caps_malloc(3000, MALLOC_CAP_SPIRAM);  // (PSRAM: internal RAM is for the camera)
    if (!ss) return;
    fileTail(SESS_FILE, ss, 3000);
    const char* lines[12];
    int nl = 0;
    for (char* line = strtok(ss, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
      if (nl == 12) { memmove(lines, lines + 1, sizeof(lines[0]) * 11); nl = 11; }
      lines[nl++] = line;
    }
    struct tm ti;
    char now[24] = "";
    if (getLocalTime(&ti, 5)) strftime(now, sizeof(now), "%Y-%m-%d %H:%M", &ti);
    File sf = SD_MMC.open(SESS_FILE, FILE_WRITE);
    if (sf) {
      for (int i = nl > 9 ? nl - 9 : 0; i < nl; i++) sf.println(lines[i]);
      sf.printf("%s | %s\n", now, summary);
      sf.close();
    }
  }
  memLoad();
  char note[120];
  snprintf(note, sizeof(note), "memory: the conversation summarised, %d facts kept", added);
  govLog("chat", note);
}

// ---- Seeing: a photo when you ask about something to look at ----
// Gemini also sends the text of what YOU say (inputTranscription), while you talk. When it has a
// "look" word ("see", "look", "what is this", "holding"...) the robot takes a photo right then and
// sends it - so Gemini has the picture before you've finished the question: no extra wait.
// (One photo per turn; the camera is on only for that.)
// (clearly about looking: "who is" and "how many" alone took photos for "who is the president")
const char* const LOOK_WORDS[] = {"what do you see", "what can you see", "can you see", "do you see", "look at",
                                  "take a look", "have a look", "what is this", "what's this", "what is that",
                                  "what's that", "what am i holding", "what am i wearing", "in my hand",
                                  "who is this", "who's this", "who is that", "who's that", "in front of you",
                                  "show you", "read this", "read that", "describe what", "what colour is this",
                                  "what color is this", "who is it", "who's it", "now who", "dekho", "dekh ke", "yeh kya hai", "ye kya hai", "yeh dekh",
                                  // Hindi as the transcript writes it - Devanagari or Roman ("देखो मुझे",
                                  // "dikh raha hun", "maine kya pehna hai" must take a photo)
                                  "look", "see me", "dekh", "dekhna", "dekhiye", "dikh", "dikhta", "dikhai", "pehna", "pehne",
                                  "देख", "देखो", "देखिए", "दिख", "दिखा", "पहना", "पहने", "ये क्या", "यह क्या", "क्या है ये", "क्या है यह"};
char (&heard)[420] = *(char(*)[420])heap_caps_calloc(1, sizeof(char) * (420), MALLOC_CAP_SPIRAM);           // what you've said this turn (lower case)
// The chat log (/sesame/chat.txt on the SD card): each turn's "you" and "Sesame" lines, so a conversation
// can be read back later. The parser task fills these; loop() writes them.
char (&logYou)[420] = *(char(*)[420])heap_caps_calloc(1, sizeof(char) * (420), MALLOC_CAP_SPIRAM);
char (&logMe)[600] = *(char(*)[600])heap_caps_calloc(1, sizeof(char) * (600), MALLOC_CAP_SPIRAM);  // (Hindi: 3 bytes a letter)
// what's logged already: a turn with a tool call ends twice (the call, then the words) - logged once,
// with your words and only the new part of Oreo's
int saidLogged = 0;
bool heardLogged = false;
volatile bool logReady = false;
volatile bool lookReq = false;  // take a photo and send it
volatile bool spkOn = false;      // the speaker playing a reply (audioTask): the camera waits for it to stop
bool lookedThisTurn = false;
volatile uint32_t looks = 0, lookFails = 0;
volatile int lookMs = -1, lookKB = 0;  // the last photo: how long from asking to sent, its size
unsigned long lookDoneMs = 0;           // (one photo per 5 s at most)
unsigned long camOnSinceMs = 0;
volatile bool photoPending = false;  // a photo is being taken: the head holds still (it moved: smeared photos)
bool stillHeld = false;              // the head is being held still for a photo turn
float headStillP = 0, headStillT = 0;
bool tlsPaused = false;  // the secure server is paused for this conversation
volatile bool quotaOut = false;  // Google said the quota is used up: the session ends, a tired face
// Google Search (in the tools) has its own, small free quota: when it's used up Google refuses the whole
// connection ("exceeded your current quota") although the voice model itself is fine. Then: connect
// again at once without Search (and try with it again an hour later).
// (On the free tier Search grounding isn't allowed at all - "0 / 0" on the AI Studio rate-limit page -
// so it's off from the start; with billing on, set this false.)
volatile bool searchOff = true;
unsigned long searchOffMs = 0;
uint8_t* lastPhoto = nullptr;           // the last photo sent (GET /gemini?photo=1 shows it)
size_t lastPhotoLen = 0;

// stats (GET /gemini)
volatile uint32_t turns = 0, connects = 0, drops = 0, errors = 0, bargeIns = 0, rxMsgs = 0, txFrames = 0, underruns = 0;
volatile int latMs = -1, connMs = -1, playDelayMs = -1, maxGapMs = 0, maxMsgKB = 0;
char lastErr[96] = "";
char dropWhy[64] = "";
char (&said)[600] = *(char(*)[600])heap_caps_calloc(1, sizeof(char) * (600), MALLOC_CAP_SPIRAM);
// the turn so far into the log (at its end, and before its words are cleared: a typed line, you
// talking, the conversation ending - an interrupted turn has no turnComplete). If loop() hasn't written
// the last one yet, this one is added to it.
void logFlush() {
  const int n = strlen(said);
  if (n <= saidLogged) return;
  if (!logReady) logYou[0] = logMe[0] = 0;
  else strlcat(logMe, " ", sizeof(logMe));
  if (!heardLogged) {
    if (logYou[0]) strlcat(logYou, " / ", sizeof(logYou));
    strlcat(logYou, heard, sizeof(logYou));
  }
  strlcat(logMe, said + saidLogged, sizeof(logMe));
  saidLogged = n;
  heardLogged = true;
  logReady = true;
}
char handle[128] = "";  // session resumption (a dropped connection carries on where it was)
volatile float inLvl = 0, outLvl = 0, coup = 1.0f, noise = 0.002f;
volatile float sessPeakIn = 0;  // the loudest mic level this conversation, while the robot wasn't talking (did it hear anyone?)
volatile int maxLagMs = 0;      // the most mic audio waiting to go up (a slow link: Google hears you late)

// ---- rings (PSRAM) ----
const uint32_t PLAY_N = 1u << 19;  // 524288 samples = 32 s at 16 kHz (1 MB)
int16_t* play = nullptr;
volatile uint32_t playW = 0, playR = 0;
const uint32_t MIC_N = 1u << 16;   // 4 s (PSRAM): what you say while it connects is kept and sent
int16_t* mic = nullptr;
volatile uint32_t micW = 0, sendR = 0;
uint8_t* dec = nullptr;            // decoded reply audio
const int DEC_MAX = 384 * 1024;
const int MSG_MAX = 512 * 1024;    // a bigger message is skipped
struct Msg { char* p; int len; };
QueueHandle_t rxQ = nullptr;       // whole messages: the network task -> the parser task

// 24 kHz reply -> 16 kHz (a [1 2 1]/4 low-pass first: above 8 kHz would fold back as hiss). The
// pitch is changed later, at playback, without changing the speed (pitchStep)
float rsPrev = 0, rsT = 0, lp1 = 0, lp2 = 0;
void push24(const int16_t* s, int n) {
  uint32_t w = playW;
  for (int i = 0; i < n; i++) {
    const float x = s[i];
    const float y = (x + 2 * lp1 + lp2) * 0.25f;
    lp2 = lp1;
    lp1 = x;
    while (rsT < 1.0f) {
      if (w - playR >= PLAY_N - 1) break;  // full (32 s queued): drop
      play[w & (PLAY_N - 1)] = (int16_t)(rsPrev + (y - rsPrev) * rsT);
      w++;
      rsT += 1.5f;
    }
    rsT -= 1.0f;
    rsPrev = y;
  }
  playW = w;
}

// Pitch shifter (two read heads in a 40 ms delay line, crossfaded): higher voice, same speed - so
// the speaker never runs ahead of Gemini (playing it faster did: gaps)
const int PS_N = 2048, PS_W = 640;
float* psBuf = nullptr;
uint32_t psW = 0;
float psD = 0;
inline float psRead(float back) {
  const float pos = (float)psW - 1.0f - back;
  const int i = (int)floorf(pos);
  const float f = pos - i;
  const float a = psBuf[i & (PS_N - 1)], b = psBuf[(i + 1) & (PS_N - 1)];
  return a + (b - a) * f;
}
// a sine table (256 steps a turn, interpolated): sinf() 4 times a sample on core 0, next to the WiFi,
// is too slow - the voice effects stutter and drop
float SIN_T[257];  // (1 KB of internal RAM)
bool sinTReady = false;
inline float sinT(float turns) {  // sin(2 pi turns), turns >= 0
  if (!sinTReady) {
    for (int i = 0; i <= 256; i++) SIN_T[i] = sinf(6.2831853f * i / 256);
    sinTReady = true;
  }
  const float p = (turns - (int)turns) * 256;
  const int i = (int)p;
  return SIN_T[i] + (SIN_T[i + 1] - SIN_T[i]) * (p - i);
}
inline float pitchStep(float x) {
  psBuf[psW & (PS_N - 1)] = x;
  psW++;
  if (pitch > 0.99f && pitch < 1.01f) return x;
  psD -= (pitch - 1.0f);  // the heads move through the window at (pitch - 1) samples per sample
  while (psD < 0) psD += PS_W;
  while (psD >= PS_W) psD -= PS_W;
  float d2 = psD + PS_W / 2;
  if (d2 >= PS_W) d2 -= PS_W;
  const float w1 = sinT(0.5f * psD / PS_W), g1 = w1 * w1;  // the other head's weight: cos^2 = 1 - sin^2
  return psRead(psD + 4) * g1 + psRead(d2 + 4) * (1 - g1);
}

// ---- the mic's bits ----
// The mic's 24 bits don't land in the same place every time the I2S driver starts (a fixed offset is
// right only on some starts: otherwise it hears scrambled bits - loud, DC, clipping). So each
// conversation finds them: ~150 ms of frames ORed together show which bits of
// the 64-bit frame (left word, right word) ever change; the mic's LSB is the last of that run, its
// MSB 23 bits earlier - maybe in the previous frame's tail. micBit = where the MSB is (-63..40).
int micBit = 0;
bool micFound = false;
uint32_t micMaskHi = 0, micMaskLo = 0;  // (GET /gemini: what the search saw)
inline int32_t micSample(uint64_t prev, uint64_t cur) {
  uint32_t v;
  if (micBit >= 0) v = (uint32_t)(cur >> (40 - micBit)) & 0xFFFFFF;
  else {
    const int np = -micBit;  // bits from the previous frame's tail
    v = (uint32_t)((prev & ((1ULL << np) - 1)) << (24 - np)) | (uint32_t)(cur >> (64 - (24 - np)));
    v &= 0xFFFFFF;
  }
  return (int32_t)(v << 8) >> 8;  // signed 24-bit
}
void micFind(int32_t* buf) {
  uint64_t m = 0;
  const int K = aud::CHUNK;
  for (int n = 0; n < aud::RATE * 15 / 100; n += K) {
    size_t r = 0;
    i2s_read(I2S_NUM_0, buf, K * 8, &r, portMAX_DELAY);
    for (int j = 0; j < (int)(r / 8); j++) m |= ((uint64_t)(uint32_t)buf[2 * j] << 32) | (uint32_t)buf[2 * j + 1];
  }
  micMaskHi = m >> 32;
  micMaskLo = (uint32_t)m;
  micFound = m != 0;
  if (!micFound) { micBit = -1; return; }  // nothing at all: the usual offset
  // stream index i = 0..63, MSB of the left word first. The run of set bits ends at the largest
  // circular gap of zeros
  auto bitAt = [&](int i) { return (m >> (63 - (i & 63))) & 1; };
  int bestGap = -1, gapEnd = 0;
  for (int st = 0; st < 64; st++) {
    if (!bitAt(st) || bitAt(st + 1)) continue;  // a run's last set bit at st
    int g = 0;
    while (g < 64 && !bitAt(st + 1 + g)) g++;
    if (g > bestGap) { bestGap = g; gapEnd = st; }
  }
  // The MSB from the run's START, not its end: the line can stay high a bit after the LSB (the DIN pulldown
  // draining it) - a 25-bit run, the end one bit late, would shift every sample: loud noise, Google never hears you.
  int len = 0;
  while (len < 64 && bitAt(gapEnd - len + 64)) len++;
  micBit = (gapEnd - len + 1 + 64) & 63;  // the MSB (negative: in the previous frame)
  if (micBit > 40) micBit -= 64;
}

// A conversation task. In the "net" build (sdkconfig: stacks may be in PSRAM) its stack goes to PSRAM:
// 16 KB of internal RAM stay free for the camera's one 32 KB block (otherwise the biggest free block in a
// chat is too small for photos). The buffers are kept and reused (a task that ended is waited for first).
struct PsTask { StackType_t* stack = nullptr; StaticTask_t* tcb = nullptr; TaskHandle_t h = nullptr; };
PsTask psAud, psRx, psNet, psFace;
bool taskStart(PsTask& t, TaskFunction_t f, const char* name, uint32_t stack, UBaseType_t prio, BaseType_t core) {
#if CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
  for (int i = 0; i < 200 && t.h && eTaskGetState(t.h) != eDeleted; i++) delay(5);  // (the last one's gone?)
  if (t.h && eTaskGetState(t.h) != eDeleted) return false;  // still running: never reuse its stack
  if (!t.stack) t.stack = (StackType_t*)heap_caps_malloc(stack, MALLOC_CAP_SPIRAM);
  if (!t.tcb) t.tcb = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (t.stack && t.tcb) {
    t.h = xTaskCreateStaticPinnedToCore(f, name, stack, nullptr, prio, t.stack, t.tcb, core);
    return t.h != nullptr;
  }
#endif
  return xTaskCreatePinnedToCore(f, name, stack, nullptr, prio, &t.h, core) == pdPASS;
}

// ---- Voice samples (the page: Talk -> "Change voice"): each voice says a short line, made once with
// Gemini's text-to-speech (the key stays on the robot), kept on the SD card (/sesame/voices/<Voice>.wav)
// and played from there - instant after the first time. Never during a chat (its memory).
volatile int vsState = 0;  // 0 idle, 1 making one, 2 the last one failed
char vsVoice[20] = "", vsErr[64] = "";
PsTask psVs;
// all of Gemini's voices and their character (the page's list and the OLED)
struct VoiceInfo { const char* name; const char* info; };
const VoiceInfo VOICES[] = {{"Zephyr", "Bright"}, {"Puck", "Upbeat"}, {"Charon", "Informative"}, {"Kore", "Firm"}, {"Fenrir", "Excitable"}, {"Leda", "Youthful"}, {"Orus", "Firm"}, {"Aoede", "Breezy"}, {"Callirrhoe", "Easy-going"}, {"Autonoe", "Bright"}, {"Enceladus", "Breathy"}, {"Iapetus", "Clear"}, {"Umbriel", "Easy-going"}, {"Algieba", "Smooth"}, {"Despina", "Smooth"}, {"Erinome", "Clear"}, {"Algenib", "Gravelly"}, {"Rasalgethi", "Informative"}, {"Laomedeia", "Upbeat"}, {"Achernar", "Soft"}, {"Alnilam", "Firm"}, {"Schedar", "Even"}, {"Gacrux", "Mature"}, {"Pulcherrima", "Forward"}, {"Achird", "Friendly"}, {"Zubenelgenubi", "Casual"}, {"Vindemiatrix", "Gentle"}, {"Sadachbia", "Lively"}, {"Sadaltager", "Knowledgeable"}, {"Sulafat", "Warm"}};
const int VOICE_N = sizeof(VOICES) / sizeof(VOICES[0]);
const char* voiceInfo(const char* v) {
  for (int i = 0; i < VOICE_N; i++) if (!strcmp(VOICES[i].name, v)) return VOICES[i].info;
  return "";
}
volatile unsigned long voiceShowUntil = 0;  // the OLED shows the voice that's playing until then
char voiceShowName[20] = "";
volatile int vsCode = 0;                    // the last text-to-speech answer (429: the quota)
volatile int vsAllDone = -1, vsAllTotal = 0;  // "download all": how far (-1 = not running)
// a voice's sample from the SD card, its name on the OLED
bool voicePlay(const char* v) {
  char path[64];
  snprintf(path, sizeof(path), "/sesame/voices/%s.wav", v);
  if (aud::playing) { aud::stopReq = true; for (int t = 0; t < 60 && aud::playing; t++) delay(20); }
  if (!aud::play(path, 0.8f)) return false;
  strlcpy(voiceShowName, v, sizeof(voiceShowName));
  voiceShowUntil = millis() + 5000;
  return true;
}
// is this voice's sample on the card (whole: a cut-off one < 40 KB is removed)?
bool voiceHave(const char* v) {
  char path[64];
  snprintf(path, sizeof(path), "/sesame/voices/%s.wav", v);
  if (!SD_MMC.exists(path)) return false;
  File f = SD_MMC.open(path);
  const size_t n = f ? f.size() : 0;
  if (f) f.close();
  if (n < 40000) { SD_MMC.remove(path); return false; }
  return true;
}
bool voiceLiveMake(const char* v, const char* path);  // (below: it uses the chat connection)
bool voiceSampleMake(bool playIt) {
  char path[64];
  snprintf(path, sizeof(path), "/sesame/voices/%s.wav", vsVoice);
  lastErr[0] = 0;
  if (voiceLiveMake(vsVoice, path)) {
    vsCode = 200;
    if (playIt && state == S_OFF) voicePlay(vsVoice);
    xlogSys("VOICE sample %s: made (Live)", vsVoice);
    return true;
  }
  xlogSys("VOICE sample %s: %s - trying text-to-speech", vsVoice, vsErr);
  char* body = (char*)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
  const int OUTN = 512 * 1024;
  char* out = (char*)heap_caps_malloc(OUTN, MALLOC_CAP_SPIRAM);
  bool ok = false;
  if (body && out) {
    snprintf(body, 1024,
             "{\"contents\":[{\"parts\":[{\"text\":\"Say it in a warm, cheerful, natural way: Hi! I'm Oreo. Do you like how I sound?\"}]}],"
             "\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],\"speechConfig\":{\"voiceConfig\":"
             "{\"prebuiltVoiceConfig\":{\"voiceName\":\"%s\"}}}}}", vsVoice);
    // up to 3 tries: no connection (memory for the TLS handshake, a WiFi hiccup), or an answer with no
    // audio in it (the TTS model now and then sends text only)
    int code = -1;
    char* d = nullptr;
    for (int k = 0; k < 3 && !d; k++) {
      if (k) delay(1500);
      code = geminiPost("gemini-2.5-flash-preview-tts", body, out, OUTN);
      vsCode = code;
      if (code == 429) break;  // (the quota: trying again now won't help)
      // ..."inlineData": {"mimeType": "audio/L16;codec=pcm;rate=24000", "data": "<base64>"}
      d = code == 200 ? strstr(out, "\"data\"") : nullptr;
      if (!d) xlogSys("VOICE sample %s try %d: HTTP %d, %u bytes: %.100s", vsVoice, k + 1, code, (unsigned)strlen(out), out);
    }
    int rate = 24000;
    if (const char* r = strstr(out, "rate=")) rate = constrain(atoi(r + 5), 8000, 48000);
    if (d) d = strchr(d + 6, '"');
    char* e = d ? strchr(d + 1, '"') : nullptr;
    size_t pcmN = 0;
    if (d && e && mbedtls_base64_decode((unsigned char*)out, OUTN, &pcmN, (const unsigned char*)d + 1, e - d - 1) == 0 && pcmN > 2000) {
      if (!SD_MMC.exists("/sesame/voices")) SD_MMC.mkdir("/sesame/voices");
      File f = SD_MMC.open(path, FILE_WRITE);
      if (f) {
        const uint32_t dl = pcmN & ~1u, br = rate * 2, rl = 36 + dl;
        uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
        const uint32_t ur = rate;
        memcpy(h + 4, &rl, 4);
        memcpy(h + 24, &ur, 4);
        memcpy(h + 28, &br, 4);
        memcpy(h + 40, &dl, 4);
        // in 4 KB pieces through an internal buffer (one big write straight from PSRAM can stop part-way)
        ok = f.write(h, 44) == 44;
        uint8_t* ib = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        for (uint32_t o = 0; ok && ib && o < dl; o += 4096) {
          const size_t n = min((uint32_t)4096, dl - o);
          memcpy(ib, out + o, n);
          bool w = false;
          for (int k = 0; k < 3 && !w; k++) { w = f.write(ib, n) == n; if (!w) delay(20); }
          ok = w;
        }
        if (!ib) ok = false;
        heap_caps_free(ib);
        f.close();
        if (!ok) SD_MMC.remove(path);
      }
      if (!ok) strlcpy(vsErr, "couldn't save it (SD card?)", sizeof(vsErr));
    } else {
      snprintf(vsErr, sizeof(vsErr), "Gemini said HTTP %d", code);
    }
  } else {
    strlcpy(vsErr, "no memory", sizeof(vsErr));
  }
  heap_caps_free(body);
  heap_caps_free(out);
  if (ok && playIt && state == S_OFF) voicePlay(vsVoice);
  xlogSys("VOICE sample %s: %s", vsVoice, ok ? "made" : vsErr);
  return ok;
}
void voiceSampleTask(void*) {
  vsState = voiceSampleMake(true) ? 0 : 2;
  vTaskDelete(nullptr);
}
// "Download all": every voice that's missing, ~3 a minute (the free text-to-speech limit); waits while
// a chat is on; stops at the daily limit
void voiceAllTask(void*) {
  vsAllTotal = VOICE_N;
  vsAllDone = 0;
  bool okAll = true;
  for (int i = 0; i < VOICE_N; i++) {
    while (state != S_OFF || starting) delay(1000);
    if (!voiceHave(VOICES[i].name)) {
      strlcpy(vsVoice, VOICES[i].name, sizeof(vsVoice));
      bool ok = voiceSampleMake(false);
      if (!ok && vsCode == 429) {  // the per-minute limit: a minute's wait, once
        delay(65000);
        ok = voiceSampleMake(false);
      }
      if (!ok && vsCode == 429) {
        strlcpy(vsErr, "Gemini's daily voice limit reached - the rest tomorrow", sizeof(vsErr));
        okAll = false;
        break;
      }
      okAll = okAll && ok;
      delay(vsCode == 200 ? 3000 : 21000);
    }
    vsAllDone = i + 1;
  }
  vsState = okAll ? 0 : 2;
  vsAllDone = -1;
  vTaskDelete(nullptr);
}

// ---- Body language: expressions and head moves that go with the words ----
// Gemini sends the text of what it says along with the audio (outputTranscription). Words in it
// cue a face or a head move ("yes" nods, "no" shakes, a question tilts, "wow" looks up surprised,
// "haha" laughs...), queued at the place in the audio where they're said and done by loop() when
// the speaker gets there - in time with the voice, no extra delay and no extra Gemini turns.
// (Not Gemini's own function calls: blocking ones make every reply ~0.7 s later, non-blocking
// ones either stop it speaking or add extra chatter.)
enum { G_NONE, G_NOD, G_SHAKE, G_TILT, G_UP, G_DOWN, G_SIDE, G_SMALL };
const char* const CUE_FACES[] = {"", "happy", "excited", "curious", "thinking", "sad", "surprised", "love", "laugh",
                                 "shy", "proud", "confused", "sleepy", "worried", "playful", "amazed", "cool"};
enum { F_NONE, F_HAPPY, F_EXCITED, F_CURIOUS, F_THINKING, F_SAD, F_SURPRISED, F_LOVE, F_LAUGH,
       F_SHY, F_PROUD, F_CONFUSED, F_SLEEPY, F_WORRIED, F_PLAYFUL, F_AMAZED, F_COOL };
struct Cue { const char* w; uint8_t face, gest; };
const Cue CUES[] = {
    {"yes", F_HAPPY, G_NOD}, {"yeah", F_HAPPY, G_NOD}, {"yep", F_HAPPY, G_NOD}, {"yup", F_HAPPY, G_NOD},
    {"sure", F_HAPPY, G_NOD}, {"absolutely", F_EXCITED, G_NOD}, {"definitely", F_EXCITED, G_NOD},
    {"exactly", F_PROUD, G_NOD}, {"correct", F_PROUD, G_NOD}, {"okay", F_HAPPY, G_NOD}, {"haan", F_HAPPY, G_NOD},
    {"no", F_NONE, G_SHAKE}, {"nope", F_PLAYFUL, G_SHAKE}, {"nah", F_PLAYFUL, G_SHAKE}, {"nahi", F_NONE, G_SHAKE},
    {"never", F_NONE, G_SHAKE},
    {"wow", F_SURPRISED, G_UP}, {"whoa", F_SURPRISED, G_UP}, {"woah", F_SURPRISED, G_UP}, {"omg", F_AMAZED, G_UP},
    {"really", F_SURPRISED, G_TILT}, {"wah", F_AMAZED, G_UP}, {"amazing", F_AMAZED, G_NONE}, {"incredible", F_AMAZED, G_UP},
    {"haha", F_LAUGH, G_SMALL}, {"hahaha", F_LAUGH, G_SMALL}, {"hehe", F_LAUGH, G_SMALL}, {"funny", F_LAUGH, G_NONE},
    {"silly", F_PLAYFUL, G_TILT}, {"joke", F_PLAYFUL, G_NONE},
    {"love", F_LOVE, G_NONE}, {"adore", F_LOVE, G_NONE}, {"favorite", F_LOVE, G_NONE}, {"favourite", F_LOVE, G_NONE},
    {"cute", F_LOVE, G_TILT}, {"aww", F_LOVE, G_TILT}, {"sweet", F_LOVE, G_NONE},
    {"sorry", F_SAD, G_DOWN}, {"sad", F_SAD, G_DOWN}, {"unfortunately", F_SAD, G_DOWN}, {"miss", F_SAD, G_NONE},
    {"hmm", F_THINKING, G_SIDE}, {"think", F_THINKING, G_NONE}, {"wonder", F_CURIOUS, G_TILT}, {"maybe", F_THINKING, G_TILT},
    {"perhaps", F_THINKING, G_TILT}, {"guess", F_THINKING, G_TILT},
    {"great", F_EXCITED, G_NONE}, {"awesome", F_EXCITED, G_NOD}, {"fantastic", F_EXCITED, G_NOD}, {"yay", F_EXCITED, G_SMALL},
    {"fun", F_EXCITED, G_NONE}, {"cool", F_COOL, G_NONE}, {"yummy", F_LOVE, G_NONE}, {"delicious", F_LOVE, G_NONE},
    {"hello", F_HAPPY, G_NOD}, {"hi", F_HAPPY, G_NOD}, {"hey", F_HAPPY, G_NOD}, {"namaste", F_HAPPY, G_NOD},
    {"sleep", F_SLEEPY, G_NONE}, {"sleepy", F_SLEEPY, G_DOWN}, {"tired", F_SLEEPY, G_DOWN}, {"yawn", F_SLEEPY, G_NONE},
    {"scary", F_WORRIED, G_NONE}, {"scared", F_WORRIED, G_NONE}, {"afraid", F_WORRIED, G_NONE}, {"uh", F_CONFUSED, G_NONE},
    {"confused", F_CONFUSED, G_TILT}, {"shy", F_SHY, G_DOWN}, {"proud", F_PROUD, G_UP}, {"thanks", F_HAPPY, G_NOD},
    {"thank", F_HAPPY, G_NOD},
};
struct Act { uint32_t at; uint8_t face, gest; };  // at: the sample of the reply where it's said
Act acts[32];
volatile uint32_t actW = 0, actR = 0;
void actPush(uint8_t face, uint8_t gest) {
  if (actW - actR >= 32) return;
  acts[actW & 31] = {playW, face, gest};
  actW++;
}
// the reply's words, piece by piece (a word can be split between pieces)
char cueWord[24];
int cueLen = 0;
bool cueSentenceStart = true;
void cueText(const char* t) {
  for (const char* c = t;; c++) {
    const unsigned char ch = (unsigned char)*c;
    if (ch && (isalpha(ch) || ch == '\'' || ch >= 0x80)) {
      if (cueLen < (int)sizeof(cueWord) - 1) cueWord[cueLen++] = (char)tolower(ch);
      continue;
    }
    if (cueLen) {
      cueWord[cueLen] = 0;
      for (const Cue& q : CUES) {
        if (strcmp(q.w, cueWord)) continue;
        // "no" mid-sentence is usually just a word ("no problem" aside): a shake only when it starts one
        if (q.gest == G_SHAKE && !cueSentenceStart && strcmp(cueWord, "never")) actPush(q.face, G_NONE);
        else actPush(q.face, q.gest);
        break;
      }
      cueLen = 0;
      cueSentenceStart = false;
    }
    if (ch == '?') actPush(F_CURIOUS, G_TILT);
    if (ch == '.' || ch == '!' || ch == '?') cueSentenceStart = true;
    if (!ch) break;
  }
}

// Head moves (head only - the legs never move): key frames around the middle, pan / tilt in
// degrees (tilt + = down), glided there by the head's spring (headSlewTo). Inside the calibrated range.
struct Key { int16_t ms; int8_t dp, dt; };
const Key K_NOD[] = {{0, 0, 9}, {170, 0, -3}, {340, 0, 6}, {520, 0, 0}, {-1, 0, 0}};
const Key K_SHAKE[] = {{0, 11, 0}, {170, -11, 0}, {340, 8, 0}, {500, -5, 0}, {650, 0, 0}, {-1, 0, 0}};
const Key K_TILT[] = {{0, 9, -5}, {1100, 0, 0}, {-1, 0, 0}};
const Key K_UP[] = {{0, 0, -11}, {1200, 0, 0}, {-1, 0, 0}};
const Key K_DOWN[] = {{0, 0, 9}, {1100, 0, 0}, {-1, 0, 0}};
const Key K_SIDE[] = {{0, 15, -8}, {1500, 0, 0}, {-1, 0, 0}};
const Key K_SMALL[] = {{0, 0, 4}, {150, 0, -1}, {300, 0, 0}, {-1, 0, 0}};
const Key* const GESTS[] = {nullptr, K_NOD, K_SHAKE, K_TILT, K_UP, K_DOWN, K_SIDE, K_SMALL};
const Key* gestNow = nullptr;
int gestStep = 0, gestSide = 1;
unsigned long gestT0 = 0, gestLastMs = 0;
float poseP = 0, poseT = 0;  // the resting pose for the state (offsets from the middle)
volatile bool emphReq = false, userTalking = false;
volatile int gestReq = 0;  // (GET /gemini?gest=1..7: try a head move)
bool headOwned = false;
unsigned long stateMs = 0;  // when the state last changed (the head glides to its new pose first)
float headLastP = 1e9, headLastT = 1e9;  // the last head target sent (1e9: send the next one)
float lookP = 0, lookT = 0;  // where you are (from the face search), degrees from the middle

// ---- audio task: speaker out (buffered, pitch), mic in, the echo gate ----
void audioTask(void*) {
  audAlive = true;
  int32_t* buf = (int32_t*)heap_caps_malloc(aud::CHUNK * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf || !aud::begin(aud::RATE, true, 4)) {
    strlcpy(lastErr, "speaker / mic didn't start", sizeof(lastErr));
    tasksRun = false;
  } else {
    aud::drop(300, buf);  // the mic's start-up
    micFind(buf);
  }
  float g = 0, refEnv = 0;
  static float fxBuf[256];
  uint32_t fxW = 0;
  // the effect's two phases, kept within one turn: sinf() of a big angle (a sample counter
  // after a minute: 30000+) is very slow - enough to hog core 0 and trip the watchdog
  float fxA = 0, fxB = 0;
  float lipPrev = 0, lipOpen = 0, lipWide = 0.5f, lipSlow = 0;
  unsigned long lastEmphMs = 0;
  float refHist[16] = {0};
  uint32_t refIdx = 0;
  int barge = 0;
  unsigned long lastRefMs = 0;
  bool barged = false, playingNow = false;
  uint64_t prevFrame = 0;
  float dcX = 0, dcY = 0;
  const int K = aud::CHUNK;
  // queued before the voice starts (no stutter); more each time it ran dry. A big enough cushion, and a
  // clean fade-out / wait / fade-in when it runs short: playing little leftover pieces with silence
  // between them clicks.
  uint32_t START = aud::RATE * 20 / 100;
  bool fadeOut = false;  // running short: this chunk fades out, then it waits for a refill
  while (tasksRun) {
    const unsigned long now = millis();
    // the speaker
    if (flushReq) {
      playR = playW;
      flushReq = false;
      playingNow = false;
    }
    uint32_t avail = playW - playR;
    // a phone on the robot's own hotspot: the replies come in bursts with gaps of seconds: a bigger
    // cushion before it speaks and after it runs dry
    static unsigned long apChkMs = 0;
    static bool apBusy = false;
    if (now - apChkMs > 1000) { apChkMs = now; apBusy = WiFi.softAPgetStationNum() > 0; }
    // (through the router too the link can deliver a reply in bursts with gaps of seconds: the face
    // talks while the speaker is silent. 0.6 s of cushion first, more after each dry spell)
    const uint32_t startMin = aud::RATE * 60 / 100, startMax = aud::RATE * 5 / 2;
    if (START < startMin) START = startMin;
    if (!playingNow && avail > 0 && (avail >= START || turnDone || (firstAudioMs && now - firstAudioMs > 1000u))) {
      playingNow = true;
      if (firstAudioMs) playDelayMs = now - firstAudioMs;
      firstAudioMs = 0;
    }
    // mid-reply with less than two chunks left: this chunk fades out and then it waits for a proper refill
    fadeOut = playingNow && !turnDone && avail < (uint32_t)(2 * K);
    if (playingNow && avail == 0) {
      playingNow = false;
      if (!turnDone) {  // ran dry mid-reply: wait for more next time
        underruns++;
        START = min<uint32_t>(START + aud::RATE * 2 / 5, startMax);
      } else {  // a reply played through: slowly back toward the short wait
        START = max<uint32_t>(START - aud::RATE / 40, startMin);
      }
    }
    const int k = playingNow ? (int)min((uint32_t)K, avail) : 0;
    float ss = 0, le = 0, ld = 0;
    // Gemini's voice is quiet next to the songs: up to 1.6x - but never more than 1.1x at a high volume
    // (1.6x at 100 % clips into a square wave - distortion - and draws the speaker's biggest current;
    // with the WiFi sending too, that can reset the robot on a part-charged battery)
    const float target = fminf(aud::masterVol * 1.6f, 1.1f);
    for (int j = 0; j < K; j++) {
      float v = 0;
      if (j < k) {
        g += (target - g) * 0.02f;
        if (fadeOut) g *= 0.985f;  // (over one chunk: ~16 ms, no click)
        const float x = play[(playR + j) & (PLAY_N - 1)];
        le += x * x;              // the lips: loudness...
        ld += (x - lipPrev) * (x - lipPrev);  // ...and how much of it is high (the change sample to sample)
        lipPrev = x;
        v = x * g;
      }
      v = pitchStep(v);
      // the robot voice: a short comb (a metallic shimmer, ~3 ms, slowly moving like a flanger) and a
      // light 80 Hz ring buzz, mixed by voiceFx
      if (voiceFx > 0.01f) {
        fxBuf[fxW & 255] = v;
        const float d = 44 + 10 * sinT(fxA);
        const float e = fxBuf[(fxW - (int)d) & 255];
        fxW++;
        fxA += 0.0004f / 6.2832f;  // (in turns)
        if (fxA >= 1) fxA -= 1;
        fxB += 80.0f / 16000;
        if (fxB >= 1) fxB -= 1;
        const float comb = v + 0.55f * e;
        const float ring = sinT(fxB);
        v = v * (1 - voiceFx) + voiceFx * comb * 0.75f * (0.8f + 0.2f * ring);
      }
      // a soft limiter: above 70 % of full scale the peaks round off instead of clipping flat
      const float av = fabsf(v), knee = 23000;
      if (av > knee) v = copysignf(knee + (32000 - knee) * tanhf((av - knee) / (32000 - knee)), v);
      ss += v * v;
      buf[2 * j] = buf[2 * j + 1] = (int32_t)v << 16;
    }
    playR += k;
    if (fadeOut) {  // faded out: stop here and wait for the cushion again (fades back in from 0)
      playingNow = false;
      g = 0;
      if (!turnDone) {
        underruns++;
        START = min<uint32_t>(START + aud::RATE * 2 / 5, startMax);
      }
    }
    if (!playingNow && avail == 0 && turnDone) g = 0;  // (the next reply fades in)
    // The mouth: open with the loudness (fast up, a bit slower down - like a jaw), shape from the
    // share of high sound (round "oo" / open "ah" low, wide "ee" / "s" high). Set as the samples go
    // out: the speaker plays them ~80 ms later, about when the face's next frame shows them.
    if (k > 0) {
      const float rms = sqrtf(le / k) / 32768.0f;
      const float hf = le > 0 ? sqrtf(ld / le) : 0;
      const float o = constrain(powf(rms / 0.12f, 0.6f) - 0.05f, 0.0f, 1.0f);
      lipOpen += (o - lipOpen) * (o > lipOpen ? 0.65f : 0.35f);
      lipWide += (constrain((hf - 0.15f) / 0.6f, 0.0f, 1.0f) - lipWide) * 0.4f;
      faceVoiceOpen = lipOpen;
      faceVoiceWide = lipWide;
      // a small nod on a stressed syllable (loud after a softer bit), now and then
      lipSlow += (rms - lipSlow) * 0.05f;
      if (rms > 0.06f && rms > 2.2f * lipSlow && now - lastEmphMs > 1400) { emphReq = true; lastEmphMs = now; }
    } else if (playingNow || !turnDone) {
      lipOpen *= 0.6f;
      faceVoiceOpen = lipOpen;
    } else {
      lipOpen = 0;
      faceVoiceOpen = -1;  // not talking: the face's own mouth
    }
    size_t w = 0;
    i2s_write(I2S_NUM_0, buf, K * 8, &w, portMAX_DELAY);
    const float ref = sqrtf(ss / K) / 32768.0f;
    outLvl = ref;

    // the mic
    size_t r = 0;
    i2s_read(I2S_NUM_0, buf, K * 8, &r, portMAX_DELAY);
    const int kk = r / 8;
    float ms = 0;
    uint32_t mw = micW;
    for (int j = 0; j < kk; j++) {
      const uint64_t cur = ((uint64_t)(uint32_t)buf[2 * j] << 32) | (uint32_t)buf[2 * j + 1];
      const float x = (float)micSample(prevFrame, cur) * (1.0f / 256);  // 16-bit scale
      prevFrame = cur;
      // high-pass ~120 Hz: the mic's DC offset (it changes every start; times the gain it would clip the
      // voice) and the servos' / room's rumble
      dcY = 0.955f * (dcY + x - dcX);
      dcX = x;
      const float s = constrain(dcY * micGain, -32767.0f, 32767.0f);
      ms += s * s;
      mic[mw & (MIC_N - 1)] = (int16_t)s;
      mw++;
    }
    micW = mw;
    const float m = kk ? sqrtf(ms / kk) / 32768.0f : 0;
    inLvl = m;
    if (!playingNow && m > sessPeakIn) sessPeakIn = m;

    // the echo gate: while the robot talks (and 250 ms after - the echo's tail) Gemini hears silence
    if (ref > 0.002f) lastRefMs = now;
    // the echo reaches the mic ~100 ms later (DMA buffers both ways, the pitch shifter): compare with
    // the loudest the robot played in the last 256 ms
    refHist[refIdx++ & 15] = ref;
    refEnv = 0;
    for (int i = 0; i < 16; i++) refEnv = fmaxf(refEnv, refHist[i]);
    const bool talking = now - lastRefMs < 450 || playingNow;  // (450 ms: its own voice's tail and the room's echo)
    if (!talking) {
      if (m < noise * 3 || noise < 0.0005f) noise = noise * 0.98f + m * 0.02f;  // the room's own level
      barge = 0;
      barged = false;
    } else if (!barged && refEnv > 0.005f) {
      // How loud the robot hears itself: a peak tracker (up fast, down slowly) - its own loudest
      // syllables must not count as you (with an average it interrupts itself, cutting its replies off).
      // Clearly louder than that for ~160 ms = you talking over it.
      const float ratio = m / refEnv;
      const bool loud = m > 2.5f * coup * refEnv + 4 * noise + 0.02f;
      if (!loud) coup += (ratio - coup) * (ratio > coup ? 0.05f : 0.005f);
      if (loud) barge++;
      else barge = max(0, barge - 2);
      if (barge >= 10) {  // ~160 ms of you over it: stop talking, listen
        barged = true;
        barge = 0;
        flushReq = true;
        // until Gemini says it stopped too - but only if its reply was still coming: after turnComplete nothing
        // would ever clear it and the whole next reply would be dropped
        discard = !turnDone;
        if (discard) discardMs = now;
        bargeIns++;
        lastRefMs = 0;
        state = S_LISTENING;
        lastVoiceMs = now;
        rewindReq = true;
      }
    }
    gateOn = talking && !barged;
    spkOn = playingNow;
  }
  free(buf);
  faceVoiceOpen = -1;
  audAlive = false;
  vTaskDelete(nullptr);
}

// ---- incoming messages (the parser task, core 1) ----
// the string value after "name": (from p), copied (JSON escapes for \" \\ \n only)
const char* strVal(const char* p, const char* name, char* out, int n) {
  const char* q = strstr(p, name);
  if (!q) return nullptr;
  q += strlen(name);
  while (*q == ' ' || *q == ':' || *q == '\n' || *q == '\r') q++;
  if (*q != '"') return nullptr;
  q++;
  int i = 0;
  while (*q && *q != '"') {
    char c = *q++;
    if (c == '\\' && *q) {
      c = *q++;
      if (c == 'n') c = ' ';
      else if (c == 'u') { q += 4; c = '\''; }  // (’ and friends: an apostrophe is close enough)
    }
    if (i < n - 1) out[i++] = c;
  }
  out[i] = 0;
  return q;
}

// A small JSON reader for tool calls (nesting-aware: an emote's "name" argument is not the function's name)
// the end ('}' / ']') of the object / array starting at p (strings skipped)
const char* jsonEnd(const char* p) {
  int d = 0;
  bool str = false;
  for (; *p; p++) {
    const char c = *p;
    if (str) { if (c == '\\' && p[1]) p++; else if (c == '"') str = false; continue; }
    if (c == '"') str = true;
    else if (c == '{' || c == '[') d++;
    else if ((c == '}' || c == ']') && --d == 0) return p;
  }
  return nullptr;
}
// where the value of the object's own key starts (not a key inside a nested object), or nullptr
const char* jsonKey(const char* o, const char* e, const char* key) {
  if (!o || !e) return nullptr;
  const size_t kl = strlen(key);
  int d = 0;
  for (const char* p = o; p < e; p++) {
    const char c = *p;
    if (c == '{' || c == '[') { d++; continue; }
    if (c == '}' || c == ']') { d--; continue; }
    if (c != '"') continue;
    const char* q = p + 1;  // a string: a key of ours?
    const bool match = d == 1 && (size_t)(e - q) > kl && !strncmp(q, key, kl) && q[kl] == '"';
    for (p = q; p < e && *p != '"'; p++) if (*p == '\\') p++;  // (to its end)
    if (match) {
      const char* v = p + 1;
      while (v < e && (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t')) v++;
      if (v < e && *v == ':') {
        v++;
        while (v < e && (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t')) v++;
        return v;
      }
    }
  }
  return nullptr;
}
// a string value (at v, from jsonKey) copied out; false if it isn't one
bool jsonStr(const char* v, char* out, int n) {
  if (!v || *v != '"') return false;
  int k = 0;
  for (v++; *v && *v != '"'; v++) {
    if (*v == '\\' && v[1]) v++;
    if (k < n - 1) out[k++] = *v;
  }
  out[k] = 0;
  return true;
}
// a look word in what you said this turn (heard, lower case): a photo for Gemini
// "bye" / "stop" by voice: the chat ends after a short bye. Longer phrases anywhere in what was heard; the
// one-word ones only when they're (nearly) the whole sentence ("stop" in "don't stop the music" isn't one)
void stopCheck(const char* heard) {  // (the turn so far, then the newest piece on its own)
  if (endAskedMs) return;
  static const char* const PHRASES[] = {"bye oreo", "bye bye", "goodbye", "good bye", "stop talking", "stop the chat",
                                        "end the chat", "end chat", "that's all", "thats all", "that is all", "go to sleep oreo",
                                        "band karo", "bas karo", "chup ho ja", "chup raho", "baat band", "बस करो", "बंद करो",
                                        "चुप हो जा", "बाय बाय", "अलविदा"};
  static const char* const WORDS[] = {"bye", "stop", "bas", "band", "enough", "बाय", "बस", "बंद"};
  for (const char* p : PHRASES) if (strstr(heard, p)) { endAskedMs = millis(); return; }
  char w[48];
  int k = 0;
  for (const char* c = heard; *c && k < (int)sizeof(w) - 1; c++)
    if (!(*c == ' ' || *c == '.' || *c == ',' || *c == '!' || *c == '?')) w[k++] = *c;
  w[k] = 0;
  if (strlen(heard) > 24) return;
  for (const char* p : WORDS) {
    const size_t n = strlen(p);
    if (!strncmp(w, p, n) && (int)strlen(w) <= (int)n + 5) { endAskedMs = millis(); return; }  // ("bye oreo", "stop it")
  }
}
void lookCheck() {
  if (lookedThisTurn) return;
  for (const char* w : LOOK_WORDS) {
    const char* f = strstr(heard, w);
    // a whole word ("see", not "seed" / "sesame"... "seesaw" aside)
    // "look left / up / around", "dekho upar": a head move (the look tool), not a photo
    // past tense is about a look already done ("mera chehra dekha tha", "देखा", "you saw me"): no new photo
    if (f) {
      const char* nx = f + strlen(w);
      // roman: "dekh"/"dikh" + a / i / e as a whole word; devanagari: + the vowel sign ा ी े, then the word ends
      // (not "dikhao" / "दिखाओ" = show me, "देखें", "देखो")
      auto ends = [](const char* c) { return !*c || *c == ' ' || *c == ',' || *c == '.' || *c == '?' || *c == '!'; };
      bool past = false;
      if (!strcmp(w, "dekh") || !strcmp(w, "dikh"))
        past = (nx[0] == 'a' || nx[0] == 'i' || nx[0] == 'e') && ends(nx + 1);
      if (!strcmp(w, "à¤¦à¥à¤") || !strcmp(w, "à¤¦à¤¿à¤"))  // देख, दिख
        past = (!strncmp(nx, "à¤¾", 3) || !strncmp(nx, "à¥", 3) || !strncmp(nx, "à¥", 3)) && ends(nx + 3);
      if (past) continue;
    }
    // a direction BEFORE the look word too - Hindi says it first: "straight dekho", "left aur right dekho", "upar dekho"
    if (f && (!strncmp(w, "dekh", 4) || !strncmp(w, "à¤¦à¥à¤", 9) || !strcmp(w, "look"))) {
      static const char* const DIRS_BEFORE[] = {"left", "right", "straight", "seedha", "seedhe", "sidha", "sidhe", "upar", "neeche", "niche",
                                                "idhar", "udhar", "peeche", "baaye", "baae", "daaye", "daae", "around", "up", "down",
                                                "à¤¸à¥à¤§à¤¾", "à¤à¤ªà¤°",
                                                "à¤¨à¥à¤à¥", "à¤¬à¤¾à¤à¤",
                                                "à¤¦à¤¾à¤à¤"};  // सीधा ऊपर नीचे बाएं दाएं
      const char* from = f - 24 > heard ? f - 24 : heard;  // the few words just before
      bool dirBefore = false;
      for (const char* d : DIRS_BEFORE) {
        const char* q = strstr(from, d);
        if (q && q < f) dirBefore = true;
      }
      if (dirBefore) continue;
    }
    if (f && (!strcmp(w, "look") || !strcmp(w, "look at") || !strncmp(w, "dekh", 4))) {
      const char* nx = f + strlen(w);
      // skip "o" (dekho), and filler words: "look at your left", "look to the right", "look towards the door"
      for (bool more = true; more;) {
        more = false;
        while (*nx == ' ' || *nx == ',' || (*nx == 'o' && nx[1] == ' ')) nx++;
        static const char* const FILL[] = {"at ", "to ", "the ", "your ", "my ", "on ", "over ", "a bit ", "little "};
        for (const char* fl : FILL) if (!strncmp(nx, fl, strlen(fl))) { nx += strlen(fl); more = true; }
      }
      static const char* const DIRS[] = {"left", "right", "up", "down", "around", "straight", "ahead", "away", "back", "center",
                                         "centre", "to the", "towards", "baay", "baae", "daay", "daae", "upar", "neeche", "niche",
                                         "idhar", "udhar", "peeche", "seedha", "seedhe", "sidha", "baaye", "daaye"};
      bool dir = false;
      for (const char* d : DIRS) if (!strncmp(nx, d, strlen(d))) dir = true;
      if (dir) continue;
    }
    if (f && (f == heard || !isalpha((unsigned char)f[-1])) && !isalpha((unsigned char)f[strlen(w)]) &&
        millis() - lookDoneMs > 5000) {
      lookReq = true;
      lookedThisTurn = true;
      break;
    }
  }
}

void onMessage(char* msg, int len) {
  msg[len] = 0;
  rxMsgs++;
  const unsigned long now = millis();
  if (strstr(msg, "\"setupComplete\"")) {
    setupDone = true;
    connMs = now - connectMs;
    if (state == S_CONNECTING) {
      state = S_LISTENING;
      lastVoiceMs = now;
    }
    return;
  }
  if (const char* h = strstr(msg, "\"newHandle\"")) strVal(h, "\"newHandle\"", handle, sizeof(handle));
  if (const char* tcall = strstr(msg, "\"toolCall\"")) {  // Gemini wants the body to do something
    strlcpy(lastToolMsg, tcall, sizeof(lastToolMsg));
    const char* fc = strstr(tcall, "\"functionCalls\"");
    const char* arr = fc ? strchr(fc, '[') : nullptr;
    const char* arrEnd = arr ? jsonEnd(arr) : nullptr;
    for (const char* o = arr ? strchr(arr, '{') : nullptr; o && arrEnd && o < arrEnd && toolW - toolR < 4; o = strchr(o + 1, '{')) {
      const char* oe = jsonEnd(o);
      if (!oe) break;
      ToolReq& q = toolQ[toolW & 3];
      memset(&q, 0, sizeof(q));
      q.num = -1;
      q.num2 = -1;
      q.flag = -1;
      jsonStr(jsonKey(o, oe, "id"), q.id, sizeof(q.id));
      jsonStr(jsonKey(o, oe, "name"), q.name, sizeof(q.name));
      if (const char* a = jsonKey(o, oe, "args")) {
        if (*a == '{') {
          const char* ae = jsonEnd(a);
          for (const char* key : {"action", "name", "direction", "expression"})
            if (jsonStr(jsonKey(a, ae, key), q.arg, sizeof(q.arg))) break;
          for (const char* key : {"seconds", "percent", "degrees", "times", "minutes"})
            if (const char* v = jsonKey(a, ae, key)) { q.num = atof(v); break; }
          if (const char* v = jsonKey(a, ae, "on_floor")) q.flag = !strncmp(v, "true", 4) ? 1 : 0;
          if (!jsonStr(jsonKey(a, ae, "fact"), q.text, sizeof(q.text)) && !jsonStr(jsonKey(a, ae, "query"), q.text, sizeof(q.text)))
            jsonStr(jsonKey(a, ae, "time"), q.text, sizeof(q.text));
          if (const char* v = jsonKey(a, ae, "more")) q.flag = !strncmp(v, "true", 4) ? 1 : 0;
          if (const char* v = jsonKey(a, ae, "shut_down")) q.flag = !strncmp(v, "true", 4) ? 1 : 0;
          // the desk tools (voice_tools.h): a second choice, a name, a second number
          for (const char* key : {"repeat", "sound"})
            if (jsonStr(jsonKey(a, ae, key), q.arg2, sizeof(q.arg2))) break;
          for (const char* key : {"label", "song"})
            if (jsonStr(jsonKey(a, ae, key), q.label, sizeof(q.label))) break;
          if (const char* v = jsonKey(a, ae, "rounds")) q.num2 = atof(v);
        }
      }
      if (q.id[0] && q.name[0]) {
        toolW++;
        toolCalls++;
      }
      o = oe;  // (the next call object, not one inside this one's args)
    }
  }
  if (strstr(msg, "\"goAway\"")) goAway = true;
  if (strstr(msg, "ACTIVITY_START")) {
    lastVoiceMs = now;
    userTalking = true;
    if (!lookReq) {  // a new turn: its own look
      logFlush();
      heard[0] = 0;
      heardLogged = false;
      lookedThisTurn = false;
    }
    if (state != S_SPEAKING) state = S_LISTENING;
  }
  if (strstr(msg, "ACTIVITY_END")) {
    userTalking = false;
    cueSentenceStart = true;
    cueLen = 0;
    userEndMs = now;
    lastVoiceMs = now;
    if (state == S_LISTENING) state = S_THINKING;
    logFlush();
    said[0] = 0;
    saidLogged = 0;
  }
  const char* it = strstr(msg, "\"interrupted\"");
  if (it && strstr(it, "true")) {  // Gemini heard you over it
    flushReq = true;
    actR = actW;  // (its moves go with it)
    discard = false;
    turnDone = true;
    state = S_LISTENING;
    lastVoiceMs = now;
  }
  // the reply's audio (base64 PCM, 24 kHz) - maybe several parts
  for (const char* p = strstr(msg, "\"inlineData\""); p; p = strstr(p, "\"inlineData\"")) {
    const char* d = strstr(p, "\"data\"");
    if (!d) break;
    d += 6;
    while (*d == ' ' || *d == ':') d++;
    if (*d != '"') break;
    d++;
    const char* e = strchr(d, '"');
    if (!e) break;
    p = e;
    if (discard) continue;
    size_t olen = 0;
    if (mbedtls_base64_decode(dec, DEC_MAX, &olen, (const unsigned char*)d, e - d) == 0 && olen >= 2) {
      if (state != S_SPEAKING) {
        if (userEndMs) latMs = now - userEndMs;
        userEndMs = 0;
        state = S_SPEAKING;
        turnDone = false;
        firstAudioMs = now;
      }
      push24((const int16_t*)dec, olen / 2);
    }
  }
  if (const char* t = strstr(msg, "\"inputTranscription\"")) {
    char piece[160];
    if (strVal(t, "\"text\"", piece, sizeof(piece))) {
      for (char* c = piece; *c; c++) *c = tolower((unsigned char)*c);
      strlcat(heard, piece, sizeof(heard));
      lookCheck();
      stopCheck(heard);
      stopCheck(piece);
    }
  }
  if (const char* t = strstr(msg, "\"outputTranscription\"")) {
    char piece[160];
    if (strVal(t, "\"text\"", piece, sizeof(piece))) {
      strlcat(said, piece, sizeof(said));
      if (!discard) cueText(piece);
    }
  }
  const char* tc = strstr(msg, "\"turnComplete\"");
  if (tc && strstr(tc, "true")) logFlush();  // (a tool call alone: nothing said yet - not yet)
  if (tc && strstr(tc, "true")) {
    turnDone = true;
    discard = false;
    turns++;
    lastVoiceMs = now;
    if (state == S_THINKING) state = S_LISTENING;  // (nothing to say)
  }
  if (const char* er = strstr(msg, "\"error\"")) {
    char m[96];
    if (strVal(er, "\"message\"", m, sizeof(m))) strlcpy(lastErr, m, sizeof(lastErr));
    errors++;
  }
}

void rxTask(void*) {
  rxAlive = true;
  Msg m;
  while (tasksRun || uxQueueMessagesWaiting(rxQ)) {
    if (xQueueReceive(rxQ, &m, pdMS_TO_TICKS(50)) != pdTRUE) continue;
    if (tasksRun) onMessage(m.p, m.len);
    heap_caps_free(m.p);
  }
  rxAlive = false;
  vTaskDelete(nullptr);
}

// ---- the connection: our own WebSocket over TLS, one task (reads and sends take turns, never
// wait on each other: a send held behind a whole reply's read can drop the connection)
const char* const HOST = "generativelanguage.googleapis.com";
const char* const PATH = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
esp_tls_t* tls = nullptr;
int sock = -1;

// each drop, for the governor's log (written by loop(): the SD card from one task)
char dropNote[140] = "";
volatile bool dropNoteReady = false;
volatile int lastWriteErr = 0;  // (the last send error: in the drop's log line)
void netDrop(const char* why) {
  if (strcmp(why, "end") != 0) ::linkCheckNow = true;  // (main.cpp: is the WiFi link itself dead?)
  if (netUp && strcmp(why, "end") != 0 && !dropNoteReady) {
    snprintf(dropNote, sizeof(dropNote), "connection dropped (%s, err -0x%04x, internal %u KB)%s%.80s", why,
             (unsigned)-(!strcmp(why, "send") ? lastWriteErr : 0), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             lastErr[0] ? ": " : "", lastErr);
    dropNoteReady = true;
  }
  if (tls) {
    esp_tls_conn_destroy(tls);
    tls = nullptr;
  }
  sock = -1;
  if (netUp) {
    drops++;
    strlcpy(dropWhy, why, sizeof(dropWhy));
  }
  netUp = setupDone = false;
}

// all n bytes, or false (timeout / error)
bool tlsWrite(const uint8_t* p, int n) {
  const unsigned long t0 = millis();
  // internal RAM nearly gone (a photo, a burst of audio in): wait a moment rather than fail -
  // the network stack can't get a buffer and the send would fail
  while (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 6000 && millis() - t0 < 1500) delay(10);
  if (!tls) return false;
  while (n > 0) {
    const int w = esp_tls_conn_write(tls, p, n);
    if (w > 0) { p += w; n -= w; continue; }
    // no buffer for a moment ("send failed", not "try again"): the record waits in mbedTLS - retry
    const bool busy = w == ESP_TLS_ERR_SSL_WANT_WRITE || w == ESP_TLS_ERR_SSL_WANT_READ || w == MBEDTLS_ERR_NET_SEND_FAILED;
    if (!busy || millis() - t0 > 4000) { lastWriteErr = w; return false; }
    delay(w == MBEDTLS_ERR_NET_SEND_FAILED ? 25 : 2);
  }
  return true;
}
// frame timing (GET /gemini?frames=1): when each of the last 32 frames arrived (ms after the last
// typed line / start), its size and how long reading it took
struct FrameLog { uint32_t t; uint32_t len; uint16_t readMs; };
FrameLog (&flog)[32] = *(FrameLog(*)[32])heap_caps_calloc(1, sizeof(FrameLog) * (32), MALLOC_CAP_SPIRAM);
volatile uint32_t flogN = 0;
volatile unsigned long flogT0 = 0;
volatile int lastReadErr = 0;
bool tlsRead(uint8_t* p, int n) {
  const unsigned long t0 = millis();
  if (!tls) return false;
  while (n > 0) {
    const int r = esp_tls_conn_read(tls, p, n);
    if (r > 0) { p += r; n -= r; continue; }
    lastReadErr = r;
    if (r == 0) return false;  // closed
    if (r != ESP_TLS_ERR_SSL_WANT_READ && r != ESP_TLS_ERR_SSL_WANT_WRITE) return false;
    if (millis() - t0 > 5000) return false;
    delay(1);
  }
  return true;
}

// one frame from us (masked, as a client must)
uint8_t* txb = nullptr;
const int TXB = 160 * 1024;  // (a photo: up to ~110 KB of base64)
bool wsSend(uint8_t op, const char* data, int len) {
  if (!tls || len > TXB - 14) return false;
  int h = 0;
  txb[h++] = 0x80 | op;
  if (len < 126) txb[h++] = 0x80 | len;
  else if (len < 65536) { txb[h++] = 0x80 | 126; txb[h++] = len >> 8; txb[h++] = len & 0xFF; }
  else {
    txb[h++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--) txb[h++] = i < 4 ? (len >> (8 * i)) & 0xFF : 0;
  }
  const uint32_t mk = esp_random();
  uint8_t* mask = txb + h;
  memcpy(mask, &mk, 4);
  h += 4;
  for (int i = 0; i < len; i++) txb[h + i] = data[i] ^ mask[i & 3];
  return tlsWrite(txb, h + len);
}

// TLS + the WebSocket upgrade

bool netConnect() {
  connects++;
  connectMs = millis();
  tls = esp_tls_init();
  if (!tls) return false;
  esp_tls_cfg_t cfg = {};
  cfg.cacert_buf = (const unsigned char*)ROOTS;
  cfg.cacert_bytes = sizeof(ROOTS);
  cfg.timeout_ms = 10000;
  if (esp_tls_conn_new_sync(HOST, strlen(HOST), 443, &cfg, tls) != 1) {
    strlcpy(lastErr, "couldn't reach Google (TLS)", sizeof(lastErr));
    netDrop("connect");
    return false;
  }
  esp_tls_get_conn_sockfd(tls, &sock);
  int one = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));  // small mic frames go now
  sockKeepAlive(sock);
  uint8_t nonce[16];
  esp_fill_random(nonce, sizeof(nonce));
  unsigned char k64[32];
  size_t kl = 0;
  mbedtls_base64_encode(k64, sizeof(k64), &kl, nonce, sizeof(nonce));
  char* req = (char*)txb;
  const int n = snprintf(req, TXB,
      "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %.*s\r\n"
      "Sec-WebSocket-Version: 13\r\nx-goog-api-key: %s\r\n\r\n",
      PATH, HOST, (int)kl, (const char*)k64, key);
  if (!tlsWrite((const uint8_t*)req, n)) { netDrop("upgrade send"); return false; }
  // the answer's header, byte by byte up to the blank line (it's short)
  char hdr[512];
  int hl = 0;
  while (hl < (int)sizeof(hdr) - 1) {
    if (!tlsRead((uint8_t*)hdr + hl, 1)) break;
    hl++;
    if (hl >= 4 && !memcmp(hdr + hl - 4, "\r\n\r\n", 4)) break;
  }
  hdr[hl] = 0;
  if (strncmp(hdr, "HTTP/1.1 101", 12) != 0) {
    char* eol = strstr(hdr, "\r\n");
    if (eol) *eol = 0;
    snprintf(lastErr, sizeof(lastErr), "Google said: %.70s", hl ? hdr : "nothing");
    errors++;
    netDrop("upgrade refused");
    return false;
  }
  netUp = true;
  return true;
}

void sendSetup() {
  char resume[180] = "{}";
  if (handle[0]) snprintf(resume, sizeof(resume), "{\"handle\":\"%s\"}", handle);
  char* out = (char*)heap_caps_malloc(24576, MALLOC_CAP_SPIRAM);
  if (!out) return;
  static char (&tools)[sizeof(TOOLS_JSON)] = *(char(*)[sizeof(TOOLS_JSON)])heap_caps_calloc(1, sizeof(char) * (sizeof(TOOLS_JSON)), MALLOC_CAP_SPIRAM);
  if (searchOff) snprintf(tools, sizeof(tools), "[%s", TOOLS_JSON + strlen("[{\"googleSearch\":{}},"));
  else strlcpy(tools, TOOLS_JSON, sizeof(tools));
  // the date and time now (the robot's clock): no search needed for "what's the time / date"
  char nowNote[96] = "";
  {
    time_t tn = time(nullptr);
    struct tm lt;
    if (tn > 1700000000 && localtime_r(&tn, &lt)) strftime(nowNote, sizeof(nowNote), " Right now it is %A %d %B %Y, %I:%M %p local time - tell times in local time unless asked about another place.", &lt);
  }
  const int len = snprintf(out, 24576,
      "{\"setup\":{\"model\":\"%s\",\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],"
      "\"speechConfig\":{\"languageCode\":\"en-IN\",\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"%s\"}}}},"
      "\"systemInstruction\":{\"parts\":[{\"text\":\"%s%s%s%s\"}]},"
      // turn detection: Gemini's own speech detection with its defaults (tuned sensitivities / silence /
      // padding made the hearing worse, not better)
      "\"outputAudioTranscription\":{},\"inputAudioTranscription\":{},"
      "\"contextWindowCompression\":{\"slidingWindow\":{}},\"tools\":%s,\"sessionResumption\":%s}}",
      MODEL, voice, personaById(preset).text, nowNote,
      memText && memText[0] ? " What you remember from earlier conversations: " : "", memText ? memText : "", tools, resume);
  if (!wsSend(0x1, out, len)) netDrop("setup send");
  heap_caps_free(out);
}

// a frame from Google, if one has started arriving (whole, then handed to the parser)
bool netReadFrame() {
  uint8_t h[10];
  if (!tlsRead(h, 2)) { netDrop("read header"); return false; }
  const uint8_t op = h[0] & 0x0F;
  uint64_t len = h[1] & 0x7F;
  if (len == 126) {
    if (!tlsRead(h + 2, 2)) { netDrop("read"); return false; }
    len = (h[2] << 8) | h[3];
  } else if (len == 127) {
    if (!tlsRead(h + 2, 8)) { netDrop("read"); return false; }
    len = 0;
    for (int i = 0; i < 8; i++) len = (len << 8) | h[2 + i];
  }
  if (len > MSG_MAX) {  // too big: read and drop it
    static uint8_t junk[512];
    while (len > 0) {
      const int n = (int)min<uint64_t>(len, sizeof(junk));
      if (!tlsRead(junk, n)) { netDrop("read"); return false; }
      len -= n;
    }
    return true;
  }
  char* p = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
  if (!p) { netDrop("no memory"); return false; }
  const unsigned long r0 = millis();
  if (len && !tlsRead((uint8_t*)p, (int)len)) { heap_caps_free(p); netDrop("read payload"); return false; }
  {
    FrameLog& f = flog[flogN & 31];
    f.t = r0 - flogT0;
    f.len = (uint32_t)len;
    f.readMs = (uint16_t)min(65535UL, millis() - r0);
    flogN++;
  }
  if (op == 0x8) {  // closed by Google: the code and why
    if (len >= 2) {
      snprintf(lastErr, sizeof(lastErr), "closed %d: %.*s", ((uint8_t)p[0] << 8) | (uint8_t)p[1], (int)min<uint64_t>(len - 2, 70), p + 2);
      // the free quota used up: no point trying again today (it resets at midnight, US Pacific time)
      if (strstr(lastErr, "quota")) {
        if (!searchOff) {  // most likely Google Search's own quota: again, without it
          searchOff = true;
          searchOffMs = millis();
          strlcpy(lastErr, "Google Search's free quota is used up: talking without live search for an hour", sizeof(lastErr));
        } else {
          strlcpy(lastErr, "Gemini's free quota is used up for now (try again later)", sizeof(lastErr));
          quotaOut = true;
        }
      }
      errors++;
      // Google's own "internal error" (1011): resuming that session tends to fail again and again -
      // the next connection starts a fresh one
      if (strstr(lastErr, "closed 1011")) handle[0] = 0;
      // "session is already connected to an existing client" (1008): the dropped connection still holds that
      // session at Google - resuming it is refused every time. A fresh one.
      if (strstr(lastErr, "closed 1008")) handle[0] = 0;
    }
    heap_caps_free(p);
    netDrop("closed by Google");
    return false;
  }
  if (op == 0x9) {  // ping: pong
    wsSend(0xA, p, (int)len);
    heap_caps_free(p);
    return true;
  }
  if (op == 0x1 || op == 0x2 || op == 0x0) {
    // Google sends a message as a frame plus a short continuation: put them back together
    static char* part = nullptr;
    static int partLen = 0;
    const bool fin = h[0] & 0x80;
    if (op != 0x0 && part) {  // (a new message while one was open: the old one is lost)
      heap_caps_free(part);
      part = nullptr;
      partLen = 0;
    }
    if (!fin || part) {
      char* np = (char*)heap_caps_realloc(part, partLen + len + 1, MALLOC_CAP_SPIRAM);
      if (!np) { heap_caps_free(p); heap_caps_free(part); part = nullptr; partLen = 0; return true; }
      memcpy(np + partLen, p, len);
      partLen += len;
      heap_caps_free(p);
      part = np;
      if (!fin) return true;
      p = part;
      len = partLen;
      part = nullptr;
      partLen = 0;
    }
    const unsigned long now = millis();
    if (state == S_SPEAKING && lastMsgMs) maxGapMs = max((int)maxGapMs, (int)(now - lastMsgMs));
    lastMsgMs = now;
    maxMsgKB = max((int)maxMsgKB, (int)(len / 1024));
    Msg m = {p, (int)len};
    if (xQueueSend(rxQ, &m, 0) != pdTRUE) heap_caps_free(p);  // (the parser is 32 behind: drop)
    return true;
  }
  heap_caps_free(p);  // (pongs)
  return true;
}

// ---- Voice samples through Gemini Live (the chat's own connection - its quota is far bigger than the
// text-to-speech model's): a short session in that voice, it says the line,
// the audio it sends is kept as /sesame/voices/<Voice>.wav. Only while no chat is on (the connection's
// globals are the chat's).
bool voiceWav(const char* path, const uint8_t* pcm, uint32_t n, int rate) {
  if (!SD_MMC.exists("/sesame/voices")) SD_MMC.mkdir("/sesame/voices");
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;
  const uint32_t dl = n & ~1u, br = rate * 2, rl = 36 + dl, ur = rate;
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                   0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
  memcpy(h + 4, &rl, 4);
  memcpy(h + 24, &ur, 4);
  memcpy(h + 28, &br, 4);
  memcpy(h + 40, &dl, 4);
  bool ok = f.write(h, 44) == 44;
  uint8_t* ib = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);  // (pieces, not one big write)
  for (uint32_t o = 0; ok && ib && o < dl; o += 4096) {
    const size_t k = min((uint32_t)4096, dl - o);
    memcpy(ib, pcm + o, k);
    bool w = false;
    for (int r = 0; r < 3 && !w; r++) { w = f.write(ib, k) == k; if (!w) delay(20); }
    ok = w;
  }
  if (!ib) ok = false;
  heap_caps_free(ib);
  f.close();
  if (!ok) SD_MMC.remove(path);
  return ok;
}
// A short Live session in voice v: the system text sys, one user turn; what it says is kept as a WAV at path,
// and its words (the transcription) in said. Voice samples use it. Everything
// big is in PSRAM; only while no chat is on (the connection's globals are the chat's).
bool liveSay(const char* v, const char* sys, const char* user, const char* path, char* said, int saidN) {
  if (state != S_OFF || starting || tasksRun) return false;
  if (said && saidN) said[0] = 0;
  const bool ownTxb = !txb;
  if (ownTxb) txb = (uint8_t*)heap_caps_malloc(TXB, MALLOC_CAP_SPIRAM);
  if (!rxQ) rxQ = xQueueCreate(32, sizeof(Msg));
  const uint32_t PCMN = 24000 * 2 * 12;  // up to 12 s at 24 kHz
  uint8_t* pcm = (uint8_t*)heap_caps_malloc(PCMN, MALLOC_CAP_SPIRAM);
  const int SN = 6144;
  char* s = (char*)heap_caps_malloc(SN, MALLOC_CAP_SPIRAM);
  uint32_t pn = 0;
  int rate = 24000;
  bool done = false;
  if (txb && rxQ && pcm && s) {
    mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
    if (netConnect()) {
      int n = snprintf(s, SN,
                       "{\"setup\":{\"model\":\"%s\",\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],"
                       "\"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"%s\"}}}},"
                       "\"outputAudioTranscription\":{},\"systemInstruction\":{\"parts\":[{\"text\":\"", MODEL, v);
      n += jsonEsc(sys, s + n, SN - n - 200);
      n += snprintf(s + n, SN - n, "\"}]}}}");
      bool sent = wsSend(0x1, s, n), asked = false;
      const unsigned long t0 = millis();
      while (sent && !done && netUp && millis() - t0 < 20000) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(sock, &rs);
        timeval tv = {0, 200000};
        if ((tls ? esp_tls_get_bytes_avail(tls) : 0) <= 0 && select(sock + 1, &rs, nullptr, nullptr, &tv) <= 0) continue;
        if (!netReadFrame()) break;
        Msg m;
        while (xQueueReceive(rxQ, &m, 0) == pdTRUE) {
          if (!asked && strstr(m.p, "\"setupComplete\"")) {
            n = snprintf(s, SN, "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"text\":\"");
            n += jsonEsc(user, s + n, SN - n - 60);
            n += snprintf(s + n, SN - n, "\"}]}],\"turnComplete\":true}}");
            asked = sent = wsSend(0x1, s, n);
          }
          if (const char* r = strstr(m.p, "rate=")) rate = constrain(atoi(r + 5), 8000, 48000);
          if (said && saidN) {
            if (const char* t = strstr(m.p, "\"outputTranscription\"")) {
              char piece[160];
              if (strVal(t, "\"text\"", piece, sizeof(piece))) strlcat(said, piece, saidN);
            }
          }
          for (char* d = strstr(m.p, "\"data\""); d; d = strstr(d + 6, "\"data\"")) {  // each audio piece
            char* q1 = strchr(d + 6, '"');
            char* q2 = q1 ? strchr(q1 + 1, '"') : nullptr;
            if (!q2) break;
            size_t got = 0;
            if (mbedtls_base64_decode(pcm + pn, PCMN - pn, &got, (const unsigned char*)q1 + 1, q2 - q1 - 1) == 0) pn += got;
            d = q2;
          }
          if (strstr(m.p, "\"turnComplete\"")) done = true;
          heap_caps_free(m.p);
        }
      }
      netDrop("end");
    }
  }
  bool ok = done && pn > 24000 && voiceWav(path, pcm, pn, rate);  // (more than half a second of voice)
  heap_caps_free(pcm);
  heap_caps_free(s);
  if (ownTxb) { heap_caps_free(txb); txb = nullptr; }
  if (!ok) snprintf(vsErr, sizeof(vsErr), "Live: %s", lastErr[0] ? lastErr : done ? "no audio" : "no answer");
  return ok;
}
bool voiceLiveMake(const char* v, const char* path) {
  return liveSay(v, "You are a voice actor. When you get a line, say exactly that line and nothing else, warm and cheerful, "
                 "in your natural voice.", "Hi! I'm Oreo. Do you like how I sound?", path, nullptr, 0);
}

// ---- the network task: connect, then read whatever arrives and send the mic, in turns ----
void netTask(void*) {
  const int N = 640;  // 40 ms per message
  char* out = (char*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
  int16_t* pcm = (int16_t*)heap_caps_malloc(N * 2 * 3, MALLOC_CAP_SPIRAM);
  unsigned char* b64 = (unsigned char*)heap_caps_malloc(6144, MALLOC_CAP_SPIRAM);
  txb = (uint8_t*)heap_caps_malloc(TXB, MALLOC_CAP_SPIRAM);
  int tries = 0;
  // The camera, for photos: on only for a photo. Streaming the whole chat slows the face
  // to 12 fps and takes internal RAM the network needs. Starting it
  // mid-chat works since its DMA buffer is kept from boot (esp_camera_keep_dma); a photo takes ~1.5 s more.
  // With face tracking on (the Talk panel's switch) it streams the whole chat: the head follows
  // you, photos are instant.
  chatCamBlock = true;    // the page's video off for the chat: it and the chat don't both fit in internal RAM
  camViewWanted = false;
  const bool camMine = true;
  const bool camLazy = camMine && !chatFaceOn;  // on only for photos
  unsigned long camWarmUntilMs = 0;  // (on after a photo until then)
  bool camWarm = false;              // (this photo: the camera was on already)
  const int sizeWas = camViewSize;
  if (camMine && camViewSize != FRAMESIZE_QVGA) { camViewSize = FRAMESIZE_QVGA; camViewSizeChanged = true; }
  if (camMine && !camLazy) {
    camViewViewerMs = millis();
    camViewWanted = true;
  }
  camFrameWanted = true;
  chatFaceWanted = true;
  taskStart(psFace, chatFaceTask, "chatFace", 10240, 1, 0);  // (it ends itself when chatFaceWanted goes)
  camOnSinceMs = millis();
  if (!out || !pcm || !b64 || !txb) {
    strlcpy(lastErr, "no memory", sizeof(lastErr));
    tasksRun = false;
  }
  while (tasksRun) {
    camViewViewerMs = millis();  // (the camera view's "nobody watching" timer: we are)
    if (!netUp) {
      // (re)connect: a dropped connection carries on with the resumption handle; 4 tries
      // the WiFi itself is reconnecting (a dead link): wait for it (up to 25 s) instead of using up the tries
      static unsigned long wifiWaitMs = 0;
      if (WiFi.status() != WL_CONNECTED && !quotaOut) {
        if (!wifiWaitMs) wifiWaitMs = millis();
        if (millis() - wifiWaitMs < 25000) { state = S_CONNECTING; delay(500); continue; }
      }
      wifiWaitMs = 0;
      if (++tries > 4 || quotaOut) {
        strlcpy(stopWhy, quotaOut ? "quota used up" : "connection lost", sizeof(stopWhy));
        break;
      }
      state = S_CONNECTING;
      if (!netConnect()) {
        delay(800);
        continue;
      }
      goAway = false;
      sendSetup();
      sendR = micW;
      continue;
    }
    // a photo was asked for: the camera on (the page's camera view path), then the 2nd new frame
    // (the 1st can be dark: exposure), base64, sent. The camera takes ~2.5 s to start (then stays on
    // ~10 s: a follow-up question's photo is quick). Never more than 6 s.
    static unsigned long lookT0 = 0;
    static uint32_t lookSeq0 = 0;
    // The photo: 640 x 480 for a moment (the chat's camera runs at 320 x 240: too blurry to recognise
    // anything - the lens is soft up close and the room often dim), back to 320 x 240 after
    static int lookSizeWas = FRAMESIZE_QVGA;
    static int lookSharpWas = -1;
    // The camera off while the robot speaks: its noise slows the WiFi 5-10x and the reply coming down
    // stalls. On again once it listens: by the time
    // you've asked the next thing it has warmed up, so a follow-up "look" is instant. (Never starting
    // while the speaker plays - see the battery note below.)
    const bool speakingNow = state == S_SPEAKING || spkOn;
    if (camLazy && camWarmUntilMs && !lookT0) {  // kept warm for 30 s of listening after a photo (face tracking off)
      if (speakingNow) {
        camViewWanted = false;
        if ((long)(camWarmUntilMs - millis()) < 20000) camWarmUntilMs = millis() + 20000;  // (the talk doesn't use it up)
      } else if ((long)(millis() - camWarmUntilMs) > 0) {
        camWarmUntilMs = 0;
        camViewWanted = false;
      } else {
        camViewWanted = true;
      }
    }
    if (camMine && !camLazy && !lookT0) camViewWanted = !speakingNow;  // face tracking: follows you while you talk
    // the camera powering up while the speaker plays ("Let me look.") dips the battery below what the
    // board survives at ~40%: the robot restarts. So a cold
    // camera waits for the speaker to stop (4 s at most)
    static unsigned long lookWaitMs = 0;
    bool lookHold = false;
    if (lookReq && !lookT0 && !camReady && spkOn) {
      if (!lookWaitMs) lookWaitMs = millis();
      lookHold = millis() - lookWaitMs < 4000;
    } else lookWaitMs = 0;
    if (lookReq && setupDone && !lookT0 && !lookHold) {
      camWarm = camLazy && camWarmUntilMs != 0;
      lookT0 = millis();
      lookSeq0 = jpgSeq;
      lookSizeWas = camViewSize;
      camViewSize = FRAMESIZE_VGA;
      camPhotoQ = 8;               // finer JPEG: the lens is soft, keep every detail it has
      lookSharpWas = camSharp;
      camSharp = 5;                // the sensor's sharpening up (0..6), back after
      camSharpChanged = true;
      camViewSizeChanged = true;
      photoPending = true;
      if (camLazy) camViewWanted = true;  // the camera on, for this photo (or still on from the last)
    }
    if (lookT0) {
      if (camLazy) camViewViewerMs = millis();  // (on while the photo's wanted)
      // the camera's been on since the chat started: the 2nd new frame (a whole one, just taken).
      // If it only just started, its exposure needs a few frames: the 6th.
      // a 640 x 480 frame (after the size change), the 3rd one at that size (exposure settled)
      static int bigFrames = 0;
      static uint32_t seenSeq = 0;
      if (jpgSeq != seenSeq) {
        seenSeq = jpgSeq;
        int w = 0, h = 0;
        xSemaphoreTake(jpgMutex, portMAX_DELAY);
        const bool ok = jpgFrameLen && jpegSize(jpgFrame, jpgFrameLen, &w, &h);
        xSemaphoreGive(jpgMutex);
        // (a frame from after the head stopped: in a dim room the camera exposes long, any move smears)
        if (ok && w == 640 && millis() - exHeadMovedMs > 400) bigFrames++;
      }
      // (just switched on: its exposure settles over the first ~second - the 9th)
      const bool fresh = bigFrames >= (camLazy && !camWarm ? 9 : 3);  // (on already: settled)
      if (fresh || millis() - lookT0 > 8000) bigFrames = 0;
      if (fresh || millis() - lookT0 > 8000) {
        bool sent = false;
        if (fresh && jpgFrame) {
          uint8_t* j = (uint8_t*)heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
          size_t jl = 0;
          if (j) {
            xSemaphoreTake(jpgMutex, portMAX_DELAY);
            jl = jpgFrameLen;
            if (jl) memcpy(j, jpgFrame, jl);
            xSemaphoreGive(jpgMutex);
          }
          char* t = j && jl ? (char*)heap_caps_malloc(TXB, MALLOC_CAP_SPIRAM) : nullptr;
          if (t) {
            // Still talking: a live camera frame (Gemini takes ~1.5 s to take one in - by the time you've
            // finished, it has). Done talking (it said "let me look"): the photo and "say what you see"
            // in one message, understood at once (a frame then a question straight after: "I can't see it").
            const bool live = userTalking;
            int k = live ? snprintf(t, TXB, "{\"realtimeInput\":{\"video\":{\"mimeType\":\"image/jpeg\",\"data\":\"")
                         : snprintf(t, TXB, "{\"clientContent\":{\"turns\":[{\"role\":\"user\",\"parts\":[{\"inlineData\":"
                                            "{\"mimeType\":\"image/jpeg\",\"data\":\"");
            size_t ol = 0;
            if (mbedtls_base64_encode((unsigned char*)t + k, TXB - k - 420, &ol, j, jl)  /* (room for the prompt after it) */ == 0) {
              k += ol;
              k += live ? snprintf(t + k, TXB - k, "\"}}}")
                        : snprintf(t + k, TXB - k, "\"}},{\"text\":\"(The photo from your camera, for what they just asked. If they asked you to look or what you see, describe it briefly, in the same language they are speaking - Hindi or Hinglish if they spoke that. If they did not ask about it, ignore the photo and do not mention it.)\"}]}],"
                                                   "\"turnComplete\":true}}");
              sent = wsSend(0x1, t, k);
              if (!sent) netDrop("send");
              lookKB = jl / 1024;
              if (!lastPhoto) lastPhoto = (uint8_t*)heap_caps_malloc(JPG_MAX, MALLOC_CAP_SPIRAM);
              if (lastPhoto) { memcpy(lastPhoto, j, jl); lastPhotoLen = jl; }
            }
          }
          heap_caps_free(t);
          heap_caps_free(j);
        }
        if (sent) {
          looks++;
          lookMs = millis() - lookT0;

        } else {
          lookFails++;
          strlcpy(lastErr, camFailed ? "camera didn't start (memory?)" : "no photo in time", sizeof(lastErr));
          // no guessing: it said "let me look" - now it knows there's nothing to look at
          const char* none = "{\"realtimeInput\":{\"text\":\"(No photo: the camera could not take one just now. "
                             "Say you could not see it this time. Do not guess what is there.)\"}}";
          if (!wsSend(0x1, none, strlen(none))) netDrop("send");
        }
        lookReq = false;
        lookT0 = 0;
        photoPending = false;
        if (camLazy) camWarmUntilMs = millis() + 30000;  // stays on 30 s: the next "look" is quick
        lookDoneMs = millis();
        camViewSize = lookSizeWas;  // back to the small pictures (the face search, less memory)
        camPhotoQ = 0;
        camViewSizeChanged = true;
        if (lookSharpWas != camSharp) {
          camSharp = lookSharpWas;
          camSharpChanged = true;
        }
      }
    }
    if (respR != respW && setupDone) {  // tool answers (loop() did the command)
      const ToolResp r = respQ[respR & 3];
      respR++;
      char t[700];
      // an action's answer (move, pose, emote, look, face, volume...): it already said what it's doing, so
      // the body / face actions are NON_BLOCKING: their result goes in SILENT - only noted, no new reply
      const bool silent = !strcmp(r.name, "move") || !strcmp(r.name, "pose") || !strcmp(r.name, "emote") || !strcmp(r.name, "look") ||
                          !strcmp(r.name, "face") || !strcmp(r.name, "set_volume") || !strcmp(r.name, "set_brightness") || !strcmp(r.name, "on_floor");
      const int len = snprintf(t, sizeof(t), "{\"toolResponse\":{\"functionResponses\":[{\"id\":\"%s\",\"name\":\"%s\","
                               "\"response\":{\"result\":\"%s\"%s}}]}}", r.id, r.name, r.result,
                               silent ? ",\"scheduling\":\"SILENT\"" : "");
      if (!wsSend(0x1, t, len)) netDrop("send");
      continue;
    }
    if (sayReq[0] && setupDone) {  // a typed line: Gemini answers it out loud
      char* t = (char*)heap_caps_malloc(512, MALLOC_CAP_SPIRAM);
      if (t) {
        const int len = snprintf(t, 512, "{\"realtimeInput\":{\"text\":\"%s\"}}", sayReq);
        userEndMs = millis();
        flogT0 = userEndMs;
        state = S_THINKING;
        flogN = 0;
        logFlush();
        strlcpy(heard, sayReq, sizeof(heard));  // (the chat log: a typed line is what you said)
        for (char* c = heard; *c; c++) *c = tolower((unsigned char)*c);
        lookedThisTurn = false;
        lookCheck();  // (a typed "look": a photo too)
        heardLogged = false;
        said[0] = 0;
        saidLogged = 0;
        sayReq[0] = 0;
        if (!wsSend(0x1, t, len)) netDrop("send");
        heap_caps_free(t);
      }
      continue;
    }
    if (goAway) {  // Google ends sessions after a while: reconnect now, the handle carries on
      goAway = false;
      netDrop("goAway");
      drops--;  // (planned)
      tries = 0;
      continue;
    }
    if (setupDone) tries = 0;
    // read: anything waiting (decrypted already, or on the socket)?
    bool readable = (tls ? esp_tls_get_bytes_avail(tls) : 0) > 0;
    if (!readable) {
      fd_set rs;
      FD_ZERO(&rs);
      FD_SET(sock, &rs);
      struct timeval tv = {0, 5000};  // 5 ms
      readable = select(sock + 1, &rs, nullptr, nullptr, &tv) > 0;
    }
    if (readable) {
      netReadFrame();
      continue;
    }
    // send: the mic, 40 ms per message (up to 120 ms at once if behind)
    // Like Siri: talk right after "hey oreo" - what the mic heard while it connected (~3 s) is kept and
    // goes up first (a fresh chat only; a reconnect starts from now)
    if (!setupDone) {
      if (!prerollPending) sendR = micW;
      continue;
    }
    if (prerollPending) {
      prerollPending = false;
      sendR = micW - min(micW - sendR, MIC_N * 3 / 4);
    }
    if (rewindReq) {
      rewindReq = false;
      sendR = micW - aud::RATE * 3 / 10;
    }
    // While the robot talks, nothing goes up (not even silence: 43 KB/s of upload slowed the reply
    // coming down) - just "the mic paused" once; Gemini hears you again when it stops (or you
    // interrupt: rewindReq above sends your first 300 ms)
    // The mic only goes up while it's your turn. From the moment you
    // stop (Gemini's ACTIVITY_END) until its reply has played, nothing goes up - the upload (43 KB/s)
    // on the same connection held up the reply coming down (sends blocked, reads waited).
    static bool paused = false;
    if (gateOn || state == S_THINKING || state == S_SPEAKING) {
      if (!paused) {
        paused = true;
        const char* ps = "{\"realtimeInput\":{\"audioStreamEnd\":true}}";
        if (!wsSend(0x1, ps, strlen(ps))) netDrop("send");
      }
      sendR = micW;
      delay(5);
      continue;
    }
    paused = false;
    if (micW - sendR > MIC_N * 7 / 8) sendR = micW - N * 2;  // fell behind (WiFi): skip ahead
    // Behind after a WiFi stall (up to a few seconds: Google would hear you late):
    // the quiet part of the backlog is dropped, your words still go up
    while (micW - sendR > (uint32_t)aud::RATE * 6 / 10) {
      float q = 0;
      for (int i = 0; i < N; i++) { const float v = mic[(sendR + i) & (MIC_N - 1)]; q += v * v; }
      if (sqrtf(q / N) / 32768.0f >= noise * 2.0f) break;
      sendR += N;
    }
    const uint32_t have = micW - sendR;
    if (have < (uint32_t)N) continue;
    const int n = (int)min<uint32_t>(have / N, 3) * N;
    const int lag = (int)(have * 1000 / aud::RATE);
    if (lag > maxLagMs) maxLagMs = lag;
    float qs = 0;
    for (int i = 0; i < n; i++) { pcm[i] = mic[(sendR + i) & (MIC_N - 1)]; qs += (float)pcm[i] * pcm[i]; }
    sendR += n;
    // Near the room's hiss for 400 ms: pure silence goes up - Google waits for real quiet to end your turn and the
    // mic's hiss (~-40 dBFS) would keep it listening long after you stopped. Speech passes untouched.
    static unsigned long quietMs = 0;
    if (sqrtf(qs / n) / 32768.0f < noise * 2.0f) quietMs += (unsigned long)n * 1000 / aud::RATE;
    else quietMs = 0;
    if (quietMs > 400) memset(pcm, 0, n * 2);
    size_t olen = 0;
    mbedtls_base64_encode(b64, 6144, &olen, (const unsigned char*)pcm, n * 2);
    const int len = snprintf(out, 8192, "{\"realtimeInput\":{\"audio\":{\"data\":\"%.*s\",\"mimeType\":\"audio/pcm;rate=16000\"}}}",
                             (int)olen, (const char*)b64);
    if (wsSend(0x1, out, len)) txFrames++;
    else netDrop("send");
  }
  // teardown: the others stop, then the connection, then the speaker / mic, the camera
  tasksRun = false;
  camFrameWanted = false;
  chatFaceWanted = false;
  chatCamBlock = false;  // (the page may have its video again)
  if (camMine) {
    camViewWanted = false;
    if (camViewSize != sizeWas) { camViewSize = sizeWas; camViewSizeChanged = true; }
  }
  for (int i = 0; i < 200 && (audAlive || rxAlive); i++) delay(10);
  if (tls && netUp) wsSend(0x8, "\x03\xe8", 2);  // a polite close (1000)
  netDrop("end");
  aud::end();
  heap_caps_free(out);
  heap_caps_free(pcm);
  heap_caps_free(b64);
  heap_caps_free(txb);
  txb = nullptr;
  state = S_OFF;
  aud::talkOn = false;
  aud::busy = false;
  running = false;
  vTaskDelete(nullptr);
}

// Download speed test (GET /gemini?dl=1, then read dlKBps): one HTTPS page from Google
volatile int dlKBps = -1, dlBytes = 0, dlMs = 0;
void dlTask(void*) {
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  esp_tls_t* t = esp_tls_init();
  esp_tls_cfg_t cfg = {};
  cfg.cacert_buf = (const unsigned char*)ROOTS;
  cfg.cacert_bytes = sizeof(ROOTS);
  cfg.timeout_ms = 10000;
  const char* host = "www.google.com";
  dlKBps = -2;
  if (t && esp_tls_conn_new_sync(host, strlen(host), 443, &cfg, t) == 1) {
    const char* rq = "GET /search?q=sesame+robot+cartoon+voice HTTP/1.1\r\nHost: www.google.com\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n";
    esp_tls_conn_write(t, rq, strlen(rq));
    uint8_t* b = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    const unsigned long t0 = millis();
    int total = 0;
    while (b && millis() - t0 < 20000) {
      const int r = esp_tls_conn_read(t, b, 4096);
      if (r > 0) total += r;
      else if (r != ESP_TLS_ERR_SSL_WANT_READ) break;
    }
    dlMs = millis() - t0;
    dlBytes = total;
    dlKBps = dlMs ? total / dlMs : 0;
    heap_caps_free(b);
  }
  if (t) esp_tls_conn_destroy(t);
  vTaskDelete(nullptr);
}

// From loop() only (it claims the speaker the same way the phone's talk does)
bool start(char* why, int n) {
  load();
  if (running) return true;
  if (!key[0]) { strlcpy(why, "no Gemini key yet", n); return false; }
  if (WiFi.status() != WL_CONNECTED) { strlcpy(why, "the robot isn't on the internet (join a WiFi)", n); return false; }
  starting = true;  // the wake word keeps off the mic now (else it takes it back 1.5 s after letting go)
  struct StartingOff { ~StartingOff() { starting = false; } } startingOff;  // (cleared on every way out)
  // a web request still running (the last chat's memory summary): it finishes first (<= 5 s)
  for (int i = 0; i < 100 && rj.state == 1; i++) delay(50);
  if (aud::wwHeld) {  // the wake word's mic first (its DMA buffers count below)
    aud::wwYield = true;
    for (int i = 0; i < 60 && aud::wwHeld; i++) delay(5);
  }
  // (first: its 18.5 KB count in the memory check below)
  // Chat mode: the secure server (https - the phone's mic, a secure RC page) pauses while Oreo talks:
  // 18.5 KB of internal RAM, so the camera and the face search fit next to the conversation. Not
  // while someone's on the secure page. It's back when the conversation ends (update()).
  if (tls_httpd) {
    httpd_handle_t h = tls_httpd;
    size_t n = 8;
    int fds[8];
    if (httpd_get_client_list(h, &n, fds) == ESP_OK && n == 0) {
      tls_httpd = nullptr;
      vTaskDelay(pdMS_TO_TICKS(100));  // (whoever just read the handle finishes first)
      httpd_ssl_stop(h);
      tlsPaused = true;
    }
  }
  // A chat takes ~29 KB of internal RAM (the camera's buffer is kept from boot and it starts only for
  // a photo): 34 KB free is enough. (Just after the wake word let go of the mic, or a memory summary
  // finishing: a moment, not "no".)
  // any way out from here without a chat: the secure server back (else it stays down until the next chat)
  auto tlsBack = []() { if (tlsPaused) { tlsPaused = false; webRestartWithTls(); } };
  for (int i = 0; i < 60 && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 34000; i++) delay(50);
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 34000) {
    strlcpy(why, "not enough memory right now (video on?)", n);
    tlsBack();
    return false;
  }
  if (!play) play = (int16_t*)heap_caps_malloc(PLAY_N * 2, MALLOC_CAP_SPIRAM);
  if (!mic) mic = (int16_t*)heap_caps_malloc(MIC_N * 2, MALLOC_CAP_SPIRAM);
  if (!dec) dec = (uint8_t*)heap_caps_malloc(DEC_MAX, MALLOC_CAP_SPIRAM);
  if (!psBuf) psBuf = (float*)heap_caps_calloc(PS_N, sizeof(float), MALLOC_CAP_SPIRAM);
  if (!rxQ) rxQ = xQueueCreate(32, sizeof(Msg));
  if (!play || !mic || !dec || !psBuf || !rxQ) { strlcpy(why, "no PSRAM for the buffers", n); tlsBack(); return false; }
  // the speaker: a song or a sound stops first
  if (aud::playing) aud::stopReq = true;
  for (int i = 0; i < 60 && aud::busy; i++) delay(10);
  if (aud::busy) { strlcpy(why, "the speaker is busy", n); tlsBack(); return false; }
  quotaOut = false;
  memLoad();
  if (sdSizeMB >= 0) {
    File cf = SD_MMC.open(CHAT_FILE, FILE_READ);
    // (a summary still waiting - the last chat's: it isn't lost, the next one covers both chats)
    if (!memSummarizeReq && !memRetryMs) logStartOff = cf ? cf.size() : 0;
    memTries = 0;
    if (cf) cf.close();
  }
  toolR = toolW;
  respR = respW;
  aud::busy = aud::talkOn = true;
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);  // TLS buffers in PSRAM (talk.h)
  playW = playR = micW = sendR = 0;
  prerollPending = true;
  endAskedMs = 0;
  rsPrev = rsT = lp1 = lp2 = 0;
  psD = 0;
  lastErr[0] = said[0] = stopWhy[0] = dropWhy[0] = 0;
  saidLogged = 0;
  heardLogged = true;  // (nothing heard yet)
  logReady = false;
  netUp = setupDone = goAway = discard = flushReq = rewindReq = gateOn = false;
  turnDone = true;
  latMs = connMs = playDelayMs = -1;
  maxGapMs = maxMsgKB = 0;
  turns = drops = underruns = bargeIns = 0;
  sessPeakIn = 0;
  maxLagMs = 0;
  firstAudioMs = 0;
  handle[0] = 0;  // a new conversation (resumption is only for drops within one)
  state = S_CONNECTING;
  startedMs = lastVoiceMs = millis();
  running = tasksRun = true;
  if (!taskStart(psAud, audioTask, "gemAud", 4096, 5, 0) || !taskStart(psRx, rxTask, "gemRx", 4096, 2, 1) ||
      !taskStart(psNet, netTask, "gemNet", 8192, 4, 0)) {
    tasksRun = false;  // (a started one ends itself)
    for (int i = 0; i < 100 && (audAlive || rxAlive); i++) delay(10);
    // nothing tears down for us (that's gemNet's job): free the speaker and "running" here, or every
    // later chat says "already running" and does nothing, and the wake word never gets the mic back
    aud::end();
    aud::busy = aud::talkOn = false;
    running = false;
    state = S_OFF;
    tlsBack();
    strlcpy(why, "couldn't start its tasks", n);
    return false;
  }
  return true;
}

void stop(const char* why) {
  if (!running || !tasksRun) return;
  strlcpy(stopWhy, why, sizeof(stopWhy));
  tasksRun = false;  // gemNet tears everything down
}

// loop(): a tool call from Gemini -> the robot's command, and the answer for Gemini
void toolAnswer(const ToolReq& q, const char* result) {
  if (respW - respR >= 4) return;
  ToolResp& r = respQ[respW & 3];
  strlcpy(r.id, q.id, sizeof(r.id));
  strlcpy(r.name, q.name, sizeof(r.name));
  strlcpy(r.result, result, sizeof(r.result));
  for (char* c = r.result; *c; c++) if (*c == '"' || *c == '\\' || (unsigned char)*c < 32) *c = (*c == '"') ? '\'' : ' ';
  respW++;
  snprintf(lastTool, sizeof(lastTool), "%s %s -> %.30s", q.name, q.arg, result);
}
bool legsOk(const ToolReq& q) {
  if (!legsAllowed) {
    toolAnswer(q, "Not done: my legs are locked because I might be on a table. Ask them to tell you when you are on the floor.");
    return false;
  }
  if (batEmpty) {  // (the page refuses moves on an empty pack; so does the voice)
    toolAnswer(q, "Not done: my battery is empty - I need to be charged before I can move.");
    return false;
  }
  if (activeMode != MODE_WALK || rcActive() || robotFallen) {
    toolAnswer(q, "Not done: I can't move my legs right now (I'm in another mode, being driven, or I fell over).");
    return false;
  }
  return true;
}
ToolReq searchReq;  // the search_web call waiting for its answer
void toolsUpdate(unsigned long now) {
  // the background requests: the worker (made once), a search's answer, a memory summary's
  static bool restUp = false;
  if (!restUp) {
    rj.body = (char*)heap_caps_malloc(REST_BODY, MALLOC_CAP_SPIRAM);
    rj.out = (char*)heap_caps_malloc(REST_OUT, MALLOC_CAP_SPIRAM);
    static PsTask psRest;
    restUp = rj.body && rj.out && taskStart(psRest, restTask, "gemRest", 10240, 1, 1)  /* (core 1, normal priority: at idle priority on core 0 a search took 14 s or never came back; on core 0 at priority 1 its TLS handshake starved the idle task - watchdog) */;
  }
  if (rj.state == 2) {
    if (rj.kind == 1) {
      char ans[400] = "";
      if (rj.code == 200 && strVal(rj.out, "\"text\"", ans, sizeof(ans))) toolAnswer(searchReq, ans);
      else {
        snprintf(ans, sizeof(ans), "The search did not work just now (error %d). Say you could not look it up.", (int)rj.code);
        toolAnswer(searchReq, ans);
      }
    } else if (rj.kind == 2) {
      memSummarizeDone();
    }
    rj.state = 0;
  }
  if (memRetryMs && (long)(millis() - memRetryMs) > 0 && state == S_OFF) {
    memRetryMs = 0;
    memSummarizeReq = true;
  }
  // (not during a chat: its web request takes ~20 KB of internal RAM)
  if (memSummarizeReq && rj.state == 0 && restUp && !aud::talkOn && !starting) memSummarizeStart();
  if (moveUntilMs && (long)(now - moveUntilMs) >= 0) {  // a timed walk / turn is over
    moveUntilMs = 0;
    currentCommand = "";
  }
  // a pose repeated ("sit and stand 5 times"): the next one when the last finished
  if (robotFallen || batEmpty) poseRepLeft = 0;  // (fell over / pack empty: the "5 times" repeat is off)
  if (poseRepLeft > 0 && currentCommand.length() == 0) {
    poseRepSit = !poseRepSit;
    currentCommand = poseRepSit ? "sit" : "stand";
    poseRepLeft--;
  }
  while (toolR != toolW) {
    const ToolReq q = toolQ[toolR & 3];
    toolR++;
    if (!strcmp(q.name, "move") || !strcmp(q.name, "pose") || !strcmp(q.name, "emote")) poseRepLeft = 0;  // a new leg command stops a repeat
    char out[160];
    if (!strcmp(q.name, "move")) {
      if (!strcmp(q.arg, "stop")) { currentCommand = ""; moveUntilMs = 0; toolAnswer(q, "Stopped."); continue; }
      if (!legsOk(q)) continue;
      // the smooth walks (straightened by the gyro; the steady gait when that's switched on everywhere)
      const char* cmd = !strcmp(q.arg, "walk_forward") ? "smooth_walk" : !strcmp(q.arg, "walk_backward") ? "smooth_walk_back"
                      : !strcmp(q.arg, "turn_left") ? "smooth_sharp_left" : !strcmp(q.arg, "turn_right") ? "smooth_sharp_right" : nullptr;
      if (!cmd) { toolAnswer(q, "Unknown move."); continue; }
      const float sec = q.num > 0 ? constrain(q.num, 1.0f, 10.0f) : 3.0f;
      currentCommand = cmd;
      moveUntilMs = now + (unsigned long)(sec * 1000);
      snprintf(out, sizeof(out), "Doing it: %s for %.0f seconds.", q.arg, sec);
      toolAnswer(q, out);
    } else if (!strcmp(q.name, "pose")) {
      if (!legsOk(q)) continue;
      const bool st = !strcmp(q.arg, "stand"), si = !strcmp(q.arg, "sit") || !strcmp(q.arg, "sit_down"),
                 el = !strcmp(q.arg, "elegant_sit");
      currentCommand = st ? "stand" : si ? "sit" : el ? "sit_elegant" : "rest";
      const int times = q.num > 1 ? constrain((int)lroundf(q.num), 1, 10) : 1;
      if (times > 1 && (st || si)) {
        poseRepSit = si;
        poseRepLeft = 2 * times - 2;  // the rest of the sits and stands, alternating, ending as asked
        snprintf(out, sizeof(out), "%s - %d times in a row, sit and stand alternating (takes ~%d s).", si ? "Sitting down" : "Standing up", times, times * 4);
        toolAnswer(q, out);
      } else toolAnswer(q, st ? "Standing up." : si ? "Sitting down." : el ? "Sitting down elegantly." : "Lying down.");
    } else if (!strcmp(q.name, "emote")) {
      if (!legsOk(q)) continue;
      const char* c = !strcmp(q.arg, "shake_hands") ? "shake" : !strcmp(q.arg, "nod_yes") ? "nodyes"
                    : !strcmp(q.arg, "nod_no") ? "nodno" : !strcmp(q.arg, "go_crazy") ? "gocrazy"
                    : !strcmp(q.arg, "play_dead") ? "dead" : !strcmp(q.arg, "tap_foot") ? "tap"
                    : !strcmp(q.arg, "look_around") ? "lookaround" : !strcmp(q.arg, "robot_dance") ? "robot"
                    : !strcmp(q.arg, "play_bow") ? "playbow" : !strcmp(q.arg, "head_tilt") ? "huh" : !strcmp(q.arg, "shake_off") ? "shakeoff"
                    : !strcmp(q.arg, "paw_swat") ? "swat" : !strcmp(q.arg, "crab_rave") ? "crabrave" : !strcmp(q.arg, "wave_both_hands") ? "wave2" : !strcmp(q.arg, "dance_to_music") ? "musicdance" : q.arg;
      moveUntilMs = 0;
      currentCommand = c;
      snprintf(out, sizeof(out), "Doing the %s move now.", q.arg);
      toolAnswer(q, out);
    } else if (!strcmp(q.name, "look")) {
      // how far: the degrees asked for (or 30 sideways / 15 up-down), from the middle - or, with "more",
      // from where it's looking now (if that look is still on). Clamped to the head's calibrated range:
      // "all the way down" really goes there, and it says when it can't go further.
      const bool expired = !lookHoldUntilMs || (long)(now - lookHoldUntilMs) >= 0;
      if (expired) lookHoldP = lookHoldT = 0;
      if (!strcmp(q.arg, "center")) {
        lookHoldP = lookHoldT = 0;
        lookP = lookT = 0;
        lookHoldUntilMs = 0;
        toolAnswer(q, "Looking straight ahead.");
        continue;
      }
      const bool side = !strcmp(q.arg, "left") || !strcmp(q.arg, "right");
      const bool more = q.flag == 1;
      const float deg = q.num > 0 ? constrain(q.num, 3.0f, 120.0f) : (more ? (side ? 15.0f : 8.0f) : (side ? 30.0f : 15.0f));
      bool limit = false;
      if (side) {
        const float sgn = (!strcmp(q.arg, "left") == panPlusIsLeft) ? 1 : -1;  // + pan = left when panPlusIsLeft
        float p = (more ? lookHoldP : 0) + sgn * deg;
        const float lo = PAN_MIN + 3 - PAN_MID, hi = PAN_MAX - 3 - PAN_MID;
        if (p < lo || p > hi) limit = true;
        lookHoldP = constrain(p, lo, hi);
      } else {
        float t = (more ? lookHoldT : 0) + (!strcmp(q.arg, "down") ? deg : -deg);  // tilt + = down
        const float lo = TILT_MIN + 3 - TILT_MID, hi = TILT_MAX - 3 - TILT_MID;
        if (t < lo || t > hi) limit = true;
        lookHoldT = constrain(t, lo, hi);
      }
      lookHoldUntilMs = now + 15000;
      snprintf(out, sizeof(out), limit ? "Looking %s as far as my neck goes." : "Looking %s now.", q.arg);
      toolAnswer(q, out);
    } else if (!strcmp(q.name, "face")) {
      char f[32];
      snprintf(f, sizeof(f), "%s%s", state == S_SPEAKING ? "talk_" : "", q.arg);
      setFaceNamed(f, true);
      faceHoldUntilMs = now + 4000;
      toolAnswer(q, "Showing it on my face.");
    } else if (!strcmp(q.name, "set_volume")) {
      const int v = constrain((int)lroundf(q.num), 0, 100);
      aud::masterVol = v / 100.0f;
      prefPut("volume", v, false);
      snprintf(out, sizeof(out), "Volume set to %d percent.", v);
      toolAnswer(q, out);
    } else if (!strcmp(q.name, "set_brightness")) {
      const int v = constrain((int)lroundf(q.num), 10, 100);
      faceBright = v / 100.0f;
      prefPut("faceBright", v, false);
      snprintf(out, sizeof(out), "Brightness set to %d percent.", v);
      toolAnswer(q, out);
    } else if (!strcmp(q.name, "status")) {
      snprintf(out, sizeof(out), "Battery %d percent%s. WiFi signal %d dBm. Legs %s.", (int)batPctShown,
               batCharging ? ", charging" : "", (int)WiFi.RSSI(), legsAllowed ? "unlocked (on the floor)" : "locked (maybe on a table)");
      toolAnswer(q, out);
    } else if (!strcmp(q.name, "search_web")) {
      if (rj.state != 0 || !restUp || !q.text[0]) { toolAnswer(q, "My search is busy, try again in a moment."); continue; }
      static char qe[300];
      jsonEsc(q.text, qe, sizeof(qe));
      snprintf(rj.body, REST_BODY, "{\"contents\":[{\"parts\":[{\"text\":\"%s. Answer in at most two short sentences with the key facts.\"}]}],"
               "\"tools\":[{\"google_search\":{}}]}", qe);
      strlcpy(rj.model, "gemini-2.5-flash", sizeof(rj.model));
      searchReq = q;
      rj.kind = 1;
      rj.state = 1;  // (answered when it comes back, above)
    } else if (!strcmp(q.name, "remember")) {
      toolAnswer(q, memAdd(q.text) ? "Saved to my memory." : "I couldn't save that (no SD card?).");
    } else if (!strcmp(q.name, "get_time")) {  // the robot's clock (NTP, its timezone setting)
      time_t tn = time(nullptr);
      struct tm lt;
      if (tn > 1700000000 && localtime_r(&tn, &lt)) strftime(out, sizeof(out), "It is %I:%M %p (local time) on %A, %d %B %Y.", &lt);
      else strlcpy(out, "My clock isn't set yet (no internet time).", sizeof(out));
      toolAnswer(q, out);
    } else if (!strcmp(q.name, "end_chat")) {
      endAskedMs = millis();
      toolAnswer(q, "Ending the chat now: say a very short bye (two or three words), nothing else.");
    } else if (!strcmp(q.name, "sleep")) {
      sleepAskedMs = millis();
      toolAnswer(q, "Going to sleep now: say a very short good night (a few words), nothing else.");
    } else if (!strcmp(q.name, "on_floor")) {
      legsAllowed = q.flag == 1;
      if (!legsAllowed) { currentCommand = ""; moveUntilMs = 0; }
      toolAnswer(q, legsAllowed ? "Legs unlocked: I'm on the floor." : "Legs locked: I'm on a table.");
    } else if (deskToolCall(q, out, sizeof(out))) {  // timer / alarm / focus / music / sleep_timer (voice_tools.h)
      toolAnswer(q, out);
    } else {
      toolAnswer(q, "I don't have that tool.");
    }
  }
}

// loop(): the face and head in time with the voice
void bodyUpdate(int s, unsigned long now) {
  static unsigned long faceMs = 0, backMs = 0;
  // the cues whose words the speaker has reached
  while (actR != actW && (int32_t)(playR - acts[actR & 31].at) >= 0) {
    if (s == S_THINKING) break;  // (a word's text comes a moment before its audio: wait for it)
    // the head is still gliding from the thinking pose: a word's move waits until it's there (else it's
    // lost in the glide - a nod of 3 degrees instead of 9)
    if (acts[actR & 31].gest && now - stateMs < 350) break;
    const Act a = acts[actR & 31];
    actR++;
    if (s != S_SPEAKING) continue;
    if (a.face && now - faceMs > 900 && (long)(now - faceHoldUntilMs) >= 0) {
      char f[24];
      snprintf(f, sizeof(f), "talk_%s", CUE_FACES[a.face]);
      setFaceNamed(f, false);
      faceMs = now;
    }
    // a word's move wins over a little emphasis nod (those fire often and would block them all)
    if (a.gest && (!gestNow || gestNow == K_SMALL) && now - gestLastMs > 700) {
      gestNow = GESTS[a.gest];
      gestStep = 0;
      gestT0 = now;
      gestSide = random(2) ? 1 : -1;
    }
  }
  if (gestReq > 0 && gestReq < 8) {
    gestNow = GESTS[gestReq];
    gestStep = 0;
    gestT0 = now;
    gestSide = 1;
  }
  gestReq = 0;
  // little nods: on stressed syllables while it talks, now and then while you talk (I'm listening)
  if (emphReq) {
    emphReq = false;
    if (s == S_SPEAKING && !gestNow && now - gestLastMs > 1200) { gestNow = K_SMALL; gestStep = 0; gestT0 = now; }
  }
  if (s == S_LISTENING && userTalking && !gestNow && now - backMs > 2600 + (unsigned long)random(1500)) {
    backMs = now;
    gestNow = K_SMALL;
    gestStep = 0;
    gestT0 = now;
  }
  // the head: only in Walk mode, standing still, nobody driving (face tracking / Explore own it)
  const bool mine = activeMode == MODE_WALK && currentCommand == "" && !rcActive();  // (an emote has the head)
  if (!mine) {
    gestNow = nullptr;
    return;
  }
  headOwned = true;
  if (photoPending || lookedThisTurn) {  // a photo turn: no nods, no following - the head stays where it is
    if (!stillHeld) {  // (where it is now, relative to the middle: that's where it stays)
      stillHeld = true;
      headStillP = emoPanNow - PAN_MID;
      headStillT = emoTiltNow - TILT_MID;
      headSlewTo(emoPanNow, emoTiltNow);
      headLastP = emoPanNow;
      headLastT = emoTiltNow;
    }
    gestNow = nullptr;
    return;
  }
  stillHeld = false;
  // Looking at you: where your face was last seen (in the last 3 s) moves the resting point - small
  // steps toward it, like the face tracking mode's, so the head turns to you and stays there; the
  // nods and looks play on top. Not while thinking (it looks up and aside then).
  if (chatFaceMs && now - chatFaceMs < 3000 && s != S_THINKING) {
    static unsigned long lastSeen = 0, movedMs = 0;
    // a new sighting, in a picture taken after the head stopped (one taken while it turned saw the
    // face where it was: correcting for it again overshot, left-right-left)
    if (chatFaceMs != lastSeen && chatFaceMs - movedMs > 400) {
      lastSeen = chatFaceMs;
      // the face's offset in the picture -> degrees (the camera sees ~60 x 45 deg); 30 % of it each time
      const float p0 = lookP, t0 = lookT;
      if (fabsf(chatFaceX) > 0.12f) lookP += chatFaceX * 30 * 0.3f;  // (the same sign as face tracking mode)
      if (fabsf(chatFaceY) > 0.15f) lookT += chatFaceY * 22 * 0.3f;
      if (lookP != p0 || lookT != t0) movedMs = now;
      lookP = constrain(lookP, -45.0f, 45.0f);
      lookT = constrain(lookT, -20.0f, 15.0f);
    }
  }
  float dp = poseP + (s == S_THINKING ? 0 : lookP), dt = poseT + (s == S_THINKING ? 0 : lookT);
  if (lookHoldUntilMs && (long)(now - lookHoldUntilMs) < 0) { dp = lookHoldP; dt = lookHoldT; }  // (look: asked to)
  if (gestNow) {
    const unsigned long e = now - gestT0;
    while (gestNow[gestStep + 1].ms >= 0 && e >= (unsigned long)gestNow[gestStep + 1].ms) gestStep++;
    dp += gestNow[gestStep].dp * gestSide;
    dt += gestNow[gestStep].dt;
    if (gestNow[gestStep + 1].ms < 0) {  // the last key: done
      if (gestNow != K_SMALL) gestLastMs = now;  // (word moves are spaced out; little nods don't count)
      gestNow = nullptr;
    }
  }
  float& lastP = headLastP;
  float& lastT = headLastT;
  const float tp = constrain(PAN_MID + dp, PAN_MIN + 3, PAN_MAX - 3);
  const float tt = constrain(TILT_MID + dt, TILT_MIN + 3, TILT_MAX - 3);
  if (tp != lastP || tt != lastT) {  // (only when the target changes: the spring does the rest)
    headSlewTo(tp, tt);
    lastP = tp;
    lastT = tt;
  }
}

void chatLogWrite() {
  if (!logReady) return;
  if (sdSizeMB >= 0) {
    if (!SD_MMC.exists("/sesame")) SD_MMC.mkdir("/sesame");
    File f = SD_MMC.open("/sesame/chat.txt", FILE_APPEND);
    if (f) {
      struct tm ti;
      char ts[24] = "";
      if (getLocalTime(&ti, 5)) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &ti);
      else snprintf(ts, sizeof(ts), "boot+%lus", millis() / 1000);  // (the clock not set yet: right after a start)
      if (logYou[0]) f.printf("[%s] you: %s\n", ts, logYou);
      f.printf("[%s] oreo: %s\n", ts, logMe);
      f.close();
    }
  }
  logReady = false;
}

// loop(): requests, faces, the quiet timeout
void update() {
  if (discard && discardMs && millis() - discardMs > 10000) discard = false;  // (a lost "interrupted": never mute the next reply)
  static int shown = -1;
  static bool lookFaceDone = false;  // (this turn's photo: the face / head set straight already)
  static unsigned long logMs = 0;
  if (wantStart) {
    wantStart = false;
    char why[64] = "";
    if (start(why, sizeof(why))) {
      govLog("chat", "conversation started");
    } else {
      strlcpy(lastErr, why, sizeof(lastErr));
      govLog("chat", why);
    }
  }
  if (wantStop) {
    wantStop = false;
    stop("asked");
  }
  // "bye" / "stop": once the bye is said (listening again, or 8 s at most) the chat ends
  if (endAskedMs && ((state == S_LISTENING && millis() - endAskedMs > 2500) || millis() - endAskedMs > 8000)) {
    endAskedMs = 0;
    if (state != S_OFF) stop("bye (asked by voice)");
  }
  // "sleep": once the good night is said (listening again, or 12 s at most) the chat ends and it sleeps
  if (sleepAskedMs && ((state == S_LISTENING && millis() - sleepAskedMs > 2500) || millis() - sleepAskedMs > 12000)) {
    sleepAskedMs = 0;
    if (state != S_OFF) stop("sleep");
    faceSleepReq = true;
  }
  const int s = state;
  faceSelfPush = s != S_OFF && activeMode == MODE_WALK;  // (face_logic.h: the face task pushes its frames)
  if (shown < 0 && s == S_OFF) shown = S_OFF;  // (at boot: leave the face alone)
  if (s != shown) {
    static const char* const FACE[] = {"idle", "connecting", "listening", "thinking", "talk_happy"};
    if (s == S_OFF && shown >= 0) {
      char note[380];
      // (+ what the mic heard: peak vs the room's noise, the bit alignment found, audio frames sent - so a
      // "didn't listen" session says whether the mic was deaf or the audio never reached Google)
      snprintf(note, sizeof(note), "conversation ended (%s): %lu turns, %lu interruptions, %lu drops (%s), %lu underruns, "
               "last reply %d ms, %s | mic peak %.3f noise %.4f (%.0f dB) bit %d mask %08lx%08lx sent %lu frames, upload lag <= %d ms",
               stopWhy[0] ? stopWhy : "?", (unsigned long)turns, (unsigned long)bargeIns,
               (unsigned long)drops, dropWhy, (unsigned long)underruns, latMs, lastErr[0] ? lastErr : "no errors",
               (float)sessPeakIn, (float)noise, 20 * log10f(fmaxf((float)sessPeakIn, 1e-5f) / fmaxf((float)noise, 1e-5f)),
               micBit, (unsigned long)micMaskHi, (unsigned long)micMaskLo, (unsigned long)txFrames, maxLagMs);
      govLog("chat", note);
    }
    // Battery Saver's face has its own mouth for the voice (saverMouth), so the style isn't switched for a
    // chat; styleBack only restores a style if one was switched
    static int styleBack = -1;
    if (s == S_OFF && styleBack >= 0) {
      faceStyleReq = styleBack;
      styleBack = -1;
    }
    if (s == S_OFF && moveUntilMs) {  // a walk Gemini started: it ends with the conversation
      moveUntilMs = 0;
      currentCommand = "";
    }
    if (s == S_OFF && shown >= 0) {  // the last words into the log, then the memory: this conversation summarised (toolsUpdate)
      logFlush();
      chatLogWrite();
      memSummarizeReq = true;
    }
    if (s == S_OFF && tlsPaused) {  // the secure server back
      tlsPaused = false;
      webRestartWithTls();
    }
    // (a photo turn: no "thinking" glance up and aside - it would look away just as it says "let me look")
    setFaceNamed(s == S_OFF && quotaOut ? "tired" : s == S_THINKING && (lookedThisTurn || photoPending) ? "listening" : FACE[s], false);
    shown = s;
    lookFaceDone = lookedThisTurn || photoPending;
    // the resting pose: looking up at you a little while listening, up and aside while thinking
    // (not in a turn with a photo: the head stays exactly where it was until the answer's done)
    poseP = 0;
    poseT = s == S_LISTENING || s == S_SPEAKING ? -3 : 0;
    if (lookedThisTurn) {
      poseP = headStillP;
      poseT = headStillT;
    } else if (s == S_THINKING) {
      gestSide = random(2) ? 1 : -1;
      poseP = 12 * gestSide;
      poseT = -7;
    }
    if (s == S_OFF && headOwned) {  // back to the middle
      lookP = lookT = 0;
      headSlewTo(PAN_MID, TILT_MID);
      headOwned = false;
      gestNow = nullptr;
    }
    if (s != S_OFF) gestNow = nullptr;  // a new state: its pose now
    stateMs = millis();
    headLastP = headLastT = 1e9;
    if (s != S_SPEAKING) actR = actW;  // (the reply's cues came in while it was thinking: keep them)
  }
  // a photo asked for after the state changed (the look word comes a moment later): straight ahead now -
  // the face, and the head held where it was (not the thinking pose's turn up and aside)
  if (s == S_THINKING && (lookedThisTurn || photoPending) && !lookFaceDone) {
    lookFaceDone = true;
    setFaceNamed("listening", false);
    poseP = headStillP;
    poseT = headStillT;
    gestNow = nullptr;
  }
  toolsUpdate(millis());
  chatLogWrite();
  if (dropNoteReady) {
    govLog("chat", dropNote);
    dropNoteReady = false;
  }
  if (s == S_OFF) return;
  const unsigned long now = millis();
  bodyUpdate(s, now);
  // the speaker drained after the reply: listening again
  if (s == S_SPEAKING && turnDone && playW == playR && !gateOn) {
    state = S_LISTENING;
    lastVoiceMs = now;
  }
  // A reply that stopped mid-way (the link stalled, no turnComplete ever came): "speaking" with nothing to play
  // waits for ever. Listening again; a dead link then ends
  // the chat with the quiet timeout.
  if (s == S_SPEAKING && !turnDone && playW == playR && now - lastMsgMs > 8000) {
    turnDone = true;
    state = S_LISTENING;
    lastVoiceMs = now;
    govLog("chat", "reply stalled 8 s (nothing from Google) - listening again");
  }
  if ((s == S_LISTENING || s == S_THINKING) && now - lastVoiceMs > (unsigned long)idleS * 1000UL) stop("quiet");
  if (rcActive()) stop("RC");
  if (latMs >= 0 && now - logMs > 60000 && s == S_LISTENING) {
    logMs = now;
    char note[160];
    snprintf(note, sizeof(note), "reply %d ms (+%d ms buffer), connect %d ms, gaps <= %d ms, %lu underruns, %lu drops, echo %.2f",
             latMs, playDelayMs, connMs, maxGapMs, (unsigned long)underruns, (unsigned long)drops, (float)coup);
    govLog("chat", note);
  }
}

}  // namespace gem


esp_err_t gemini_handler(httpd_req_t* req) {
  gem::load();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (req->method == HTTP_POST && req->content_len > 8 && req->content_len < 6000) {  // your own personality
    char* pb = (char*)heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_SPIRAM);
    int got = 0;
    while (pb && got < (int)req->content_len) {
      const int r = httpd_req_recv(req, pb + got, req->content_len - got);
      if (r <= 0) break;
      got += r;
    }
    if (pb && (!strncmp(pb, "persona=", 8) || !strncmp(pb, "personaDel=", 11))) {
      const bool del = pb[7] != '=';
      char* f[4] = {pb + (del ? 11 : 8), (char*)"", (char*)"", nullptr};
      int k = 1;
      for (char* p = f[0]; *p && k < 4; p++) if (*p == '\t') { *p = 0; f[k++] = p + 1; }
      char id[16];  // a-z 0-9 _ only, "my_" in front: never one of the built-in ids
      int o = snprintf(id, sizeof(id), "my_");
      for (const char* c = f[0]; *c && o < (int)sizeof(id) - 1; c++) {
        const char ch = (char)tolower((unsigned char)*c);
        if (isalnum((unsigned char)ch) || ch == '_') id[o++] = ch;
      }
      id[o] = 0;
      if (!strncmp(f[0], "my_", 3)) strlcpy(id, f[0], sizeof(id));
      const bool ok = del ? gem::customSave(id, "", "", nullptr)
                          : k == 4 && f[3][0] && gem::customSave(id, f[1], f[2][0] ? f[2] : "Puck", f[3]);
      heap_caps_free(pb);
      char r[80];
      snprintf(r, sizeof(r), "{\"ok\":%d,\"id\":\"%s\"}", ok ? 1 : 0, id);
      return httpd_resp_sendstr(req, r);
    }
    if (pb && !strncmp(pb, "key=", 4)) {  // (a long body that's the key: below)
      char b[240] = {0};
      strlcpy(b, pb, sizeof(b));
      heap_caps_free(pb);
      char* kk = b + 4;
      while (*kk == ' ') kk++;
      for (char* e = kk + strlen(kk) - 1; e >= kk && (*e == '\n' || *e == '\r' || *e == ' '); e--) *e = 0;
      strlcpy(gem::key, kk, sizeof(gem::key));
      prefs.putString("gemKey", gem::key);
      return httpd_resp_sendstr(req, "{\"ok\":1}");
    }
    heap_caps_free(pb);
    return httpd_resp_sendstr(req, "{\"ok\":0}");
  }
  if (req->method == HTTP_POST) {  // the key, in the body ("key=...")
    char b[240] = {0};
    const int n = httpd_req_recv(req, b, min((int)req->content_len, (int)sizeof(b) - 1));
    if (n > 4 && !strncmp(b, "key=", 4)) {
      char* k = b + 4;
      while (*k == ' ') k++;
      for (char* e = k + strlen(k) - 1; e >= k && (*e == '\n' || *e == '\r' || *e == ' '); e--) *e = 0;
      strlcpy(gem::key, k, sizeof(gem::key));
      prefs.putString("gemKey", gem::key);
      return httpd_resp_sendstr(req, "{\"ok\":1}");
    }
    return httpd_resp_sendstr(req, "{\"ok\":0,\"err\":\"send key=...\"}");
  }
  char q[400] = {0}, v[32];
  httpd_req_get_url_query_str(req, q, sizeof(q));
  if (httpd_query_key_value(q, "start", v, sizeof(v)) == ESP_OK) gem::wantStart = true;
  if (httpd_query_key_value(q, "stop", v, sizeof(v)) == ESP_OK) gem::wantStop = true;
  char say[200];
  if (httpd_query_key_value(q, "say", say, sizeof(say)) == ESP_OK && say[0]) {  // URL-encoded text
    char* o = gem::sayReq;
    for (const char* c = say; *c && o < gem::sayReq + sizeof(gem::sayReq) - 1; c++) {
      char ch = *c;
      if (ch == '+') ch = ' ';
      else if (ch == '%' && isxdigit((unsigned char)c[1]) && isxdigit((unsigned char)c[2])) {
        const char hx[3] = {c[1], c[2], 0};
        ch = (char)strtol(hx, nullptr, 16);
        c += 2;
      }
      if (ch == '"' || ch == '\\' || (unsigned char)ch < 32) ch = ' ';
      *o++ = ch;
    }
    *o = 0;
  }
  if (httpd_query_key_value(q, "voice", v, sizeof(v)) == ESP_OK && v[0]) {
    bool ok = true;
    for (const char* c = v; *c; c++) ok = ok && isalpha((unsigned char)*c);
    if (ok) {
      strlcpy(gem::voice, v, sizeof(gem::voice));
      prefs.putString("gemVoice", gem::voice);
    }
  }
  if (httpd_query_key_value(q, "voicesample", v, sizeof(v)) == ESP_OK) {  // play one voice's sample (made the 1st time)
    char vn[20];
    int o = 0;
    for (const char* c = v; *c && o < (int)sizeof(vn) - 1; c++) if (isalnum((unsigned char)*c)) vn[o++] = *c;
    vn[o] = 0;
    char path[64];
    snprintf(path, sizeof(path), "/sesame/voices/%s.wav", vn);
    const char* r;
    if (!vn[0]) r = "{\"ok\":0,\"err\":\"which voice?\"}";
    else if (gem::state != gem::S_OFF || gem::starting) r = "{\"ok\":0,\"err\":\"not during a chat\"}";
    else if (sdSizeMB < 0) r = "{\"ok\":0,\"err\":\"no SD card\"}";
    else if (gem::voiceHave(vn)) {
      if (aud::playing) { aud::stopReq = true; for (int t = 0; t < 60 && aud::playing; t++) delay(20); }
      r = gem::voicePlay(vn) ? "{\"ok\":1,\"playing\":1}" : "{\"ok\":0,\"err\":\"the speaker is busy\"}";
    } else if (!gem::key[0]) r = "{\"ok\":0,\"err\":\"no Gemini key\"}";
    else if (gem::vsState == 1) r = "{\"ok\":0,\"err\":\"still making the last one\"}";
    else {
      strlcpy(gem::vsVoice, vn, sizeof(gem::vsVoice));
      gem::vsErr[0] = 0;
      gem::vsState = 1;
      // (an internal-RAM stack: the SD card write failed from a PSRAM stack; ~10 KB for a few seconds)
      if (xTaskCreatePinnedToCore(gem::voiceSampleTask, "voiceSample", 10240, nullptr, 1, nullptr, 0) != pdPASS) { gem::vsState = 2; r = "{\"ok\":0,\"err\":\"couldn't start\"}"; }
      else r = "{\"ok\":1,\"making\":1}";
    }
    return httpd_resp_sendstr(req, r);
  }
  if (httpd_query_key_value(q, "voicesall", v, sizeof(v)) == ESP_OK) {  // make every missing sample (a few minutes)
    const char* r;
    if (sdSizeMB < 0) r = "{\"ok\":0,\"err\":\"no SD card\"}";
    else if (!gem::key[0]) r = "{\"ok\":0,\"err\":\"no Gemini key\"}";
    else if (gem::vsState == 1) r = "{\"ok\":0,\"err\":\"already making samples\"}";
    else {
      gem::vsErr[0] = 0;
      gem::vsState = 1;
      if (xTaskCreatePinnedToCore(gem::voiceAllTask, "voiceAll", 10240, nullptr, 1, nullptr, 0) != pdPASS) { gem::vsState = 2; r = "{\"ok\":0,\"err\":\"couldn't start\"}"; }
      else r = "{\"ok\":1}";
    }
    return httpd_resp_sendstr(req, r);
  }
  if (httpd_query_key_value(q, "vsstatus", v, sizeof(v)) == ESP_OK) {
    char o[400];
    int k = snprintf(o, sizeof(o), "{\"state\":%d,\"voice\":\"%s\",\"err\":\"%s\",\"playing\":%d,\"allDone\":%d,\"allTotal\":%d,\"have\":[",
                     (int)gem::vsState, gem::vsVoice, gem::vsErr, aud::playing ? 1 : 0, (int)gem::vsAllDone, (int)gem::vsAllTotal);
    if (strstr(q, "have=1") && sdSizeMB >= 0) {  // (which samples are on the card)
      bool first = true;
      for (int i = 0; i < gem::VOICE_N && k < (int)sizeof(o) - 24; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/sesame/voices/%s.wav", gem::VOICES[i].name);
        if (SD_MMC.exists(p)) { k += snprintf(o + k, sizeof(o) - k, "%s\"%s\"", first ? "" : ",", gem::VOICES[i].name); first = false; }
      }
    }
    snprintf(o + k, sizeof(o) - k, "]}");
    return httpd_resp_sendstr(req, o);
  }
  if (httpd_query_key_value(q, "personas", v, sizeof(v)) == ESP_OK) {  // the personalities, for the page's dropdown
    static char (&pl)[4096] = *(char(*)[4096])heap_caps_calloc(1, sizeof(char) * (4096), MALLOC_CAP_SPIRAM);
    int k = snprintf(pl, sizeof(pl), "[");
    for (int i = 0; i < gem::PERSONA_N && k < (int)sizeof(pl) - 80; i++)
      k += snprintf(pl + k, sizeof(pl) - k, "%s{\"id\":\"%s\",\"name\":\"%s\",\"voice\":\"%s\"}", i ? "," : "",
                    gem::PERSONAS[i].id, gem::PERSONAS[i].name, gem::PERSONAS[i].voice);
    for (int i = 0; i < gem::customN && k < (int)sizeof(pl) - 120; i++)
      k += snprintf(pl + k, sizeof(pl) - k, ",{\"id\":\"%s\",\"name\":\"%s\",\"voice\":\"%s\",\"custom\":1}",
                    gem::customP[i].id, gem::customP[i].name, gem::customP[i].voice);
    snprintf(pl + k, sizeof(pl) - k, "]");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, pl);
  }
  if (httpd_query_key_value(q, "preset", v, sizeof(v)) == ESP_OK) {  // the persona; a voice + pitch to go with it
    const gem::Persona& p = gem::personaById(v);  // (the next conversation; with its voice, pitch, effect)
    strlcpy(gem::preset, p.id, sizeof(gem::preset));
    prefs.putString("gemPreset", gem::preset);
    strlcpy(gem::voice, p.voice, sizeof(gem::voice));
    prefs.putString("gemVoice", gem::voice);
    gem::pitch = p.pitch;
    gem::voiceFx = p.fx;
    prefs.putFloat("gemFx", gem::voiceFx);
    prefs.putFloat("gemPitch", gem::pitch);
  }
  if (httpd_query_key_value(q, "pitch", v, sizeof(v)) == ESP_OK) {
    gem::pitch = constrain(atof(v), 0.8, 1.6);
    prefs.putFloat("gemPitch", gem::pitch);
  }
  if (httpd_query_key_value(q, "fx", v, sizeof(v)) == ESP_OK) {
    gem::voiceFx = constrain(atof(v), 0.0, 1.0);
    prefs.putFloat("gemFx", gem::voiceFx);
  }
  if (httpd_query_key_value(q, "gain", v, sizeof(v)) == ESP_OK) {
    gem::micGain = constrain(atof(v), 1.0, 16.0);
    prefs.putFloat("gemGain", gem::micGain);
  }
  if (httpd_query_key_value(q, "idle", v, sizeof(v)) == ESP_OK) {
    gem::idleS = constrain(atoi(v), 10, 600);
    prefs.putInt("gemIdle", gem::idleS);
  }
  if (httpd_query_key_value(q, "mic", v, sizeof(v)) == ESP_OK && gem::mic) {  // the last 2 s heard, as a WAV
    httpd_resp_set_type(req, "audio/wav");
    const uint32_t n = gem::MIC_N, end = gem::micW;
    uint8_t hd[44];
    const uint32_t bytes = n * 2;
    memcpy(hd, "RIFF", 4); const uint32_t rl = 36 + bytes; memcpy(hd + 4, &rl, 4); memcpy(hd + 8, "WAVEfmt ", 8);
    const uint32_t f16 = 16; memcpy(hd + 16, &f16, 4); const uint16_t pcm = 1, ch = 1; memcpy(hd + 20, &pcm, 2); memcpy(hd + 22, &ch, 2);
    const uint32_t sr = aud::RATE, br = aud::RATE * 2; memcpy(hd + 24, &sr, 4); memcpy(hd + 28, &br, 4);
    const uint16_t ba = 2, bits = 16; memcpy(hd + 32, &ba, 2); memcpy(hd + 34, &bits, 2); memcpy(hd + 36, "data", 4); memcpy(hd + 40, &bytes, 4);
    httpd_resp_send_chunk(req, (const char*)hd, 44);
    static int16_t (&chunk)[1024] = *(int16_t(*)[1024])heap_caps_calloc(1, sizeof(int16_t) * (1024), MALLOC_CAP_SPIRAM);
    for (uint32_t i = 0; i < n; i += 1024) {
      for (int j = 0; j < 1024; j++) chunk[j] = gem::mic[(end + i + j) & (gem::MIC_N - 1)];
      httpd_resp_send_chunk(req, (const char*)chunk, sizeof(chunk));
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
  }
  if (httpd_query_key_value(q, "gest", v, sizeof(v)) == ESP_OK) gem::gestReq = atoi(v);
  if (httpd_query_key_value(q, "ww", v, sizeof(v)) == ESP_OK) {  // the wake word on / off (saved)
    ww::enabled = v[0] == '1';
    prefs.putBool("wwOn", ww::enabled);
  }
  if (httpd_query_key_value(q, "wwcut", v, sizeof(v)) == ESP_OK) {
    ww::cutoff = constrain(atof(v), 0.3, 0.995);
    prefs.putFloat("wwCut", ww::cutoff);
  }
  if (httpd_query_key_value(q, "wwwin", v, sizeof(v)) == ESP_OK) {
    ww::window = constrain(atoi(v), 1, 20);
    prefs.putInt("wwWin", ww::window);
  }
  if (httpd_query_key_value(q, "fs", v, sizeof(v)) == ESP_OK) {  // face tracking in a chat (from the next chat)
    chatFaceOn = v[0] == '1';
    prefs.putBool("gemFT", chatFaceOn);
  }
  if (httpd_query_key_value(q, "legs", v, sizeof(v)) == ESP_OK) gem::legsAllowed = v[0] == '1';  // (the page's switch)
  // (test: a tool call in Gemini's own format, as if it had sent it: ?tooltest=face:happy / status: / emote:wave)
  char tt[48];
  if (httpd_query_key_value(q, "tooltest", tt, sizeof(tt)) == ESP_OK) {
    char* colon = strchr(tt, ':');
    if (colon) {
      *colon = 0;
      const char* arg = colon + 1;
      const char* key = !strcmp(tt, "move") ? "action" : !strcmp(tt, "look") ? "direction" : !strcmp(tt, "face") ? "expression" : "name";
      static char (&fake)[400] = *(char(*)[400])heap_caps_calloc(1, sizeof(char) * (400), MALLOC_CAP_SPIRAM);
      const int fl = snprintf(fake, sizeof(fake), "{ \"toolCall\": { \"functionCalls\": [ { \"id\": \"test-%lu\", \"name\": \"%s\", \"args\": { \"%s\": \"%s\" } } ] } }", millis(), tt, key, arg);
      gem::onMessage(fake, fl);
    }
  }
  if (httpd_query_key_value(q, "look", v, sizeof(v)) == ESP_OK) {  // (test: send a photo now)
    gem::lookReq = true;
    gem::lookedThisTurn = true;
  }
  if (httpd_query_key_value(q, "forget", v, sizeof(v)) == ESP_OK && v[0] == '1') {
    SD_MMC.remove(gem::PROFILE_FILE);
    SD_MMC.remove(gem::SESS_FILE);
    SD_MMC.remove(gem::MEM_FILE);
    gem::memLoad();
  }
  if (httpd_query_key_value(q, "memory", v, sizeof(v)) == ESP_OK) {  // what Oreo remembers
    if (!gem::memText) gem::memLoad();
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, gem::memText && gem::memText[0] ? gem::memText : "(nothing yet)");
  }
  if (httpd_query_key_value(q, "crash", v, sizeof(v)) == ESP_OK) {  // the last crash, from the dump in flash
    static char (&co)[600] = *(char(*)[600])heap_caps_calloc(1, sizeof(char) * (600), MALLOC_CAP_SPIRAM);
    int k = snprintf(co, sizeof(co), "{\"dump\":");
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    esp_core_dump_summary_t* sm = (esp_core_dump_summary_t*)heap_caps_calloc(1, sizeof(esp_core_dump_summary_t), MALLOC_CAP_SPIRAM);
    if (sm && esp_core_dump_get_summary(sm) == ESP_OK) {
      k += snprintf(co + k, sizeof(co) - k, "1,\"task\":\"%s\",\"pc\":\"0x%08lx\",\"cause\":%lu,\"vaddr\":\"0x%08lx\",\"bt\":[",
                    sm->exc_task, (unsigned long)sm->exc_pc, (unsigned long)sm->ex_info.exc_cause, (unsigned long)sm->ex_info.exc_vaddr);
      for (int i = 0; i < (int)sm->exc_bt_info.depth && i < 16; i++)
        k += snprintf(co + k, sizeof(co) - k, "%s\"0x%08lx\"", i ? "," : "", (unsigned long)sm->exc_bt_info.bt[i]);
      k += snprintf(co + k, sizeof(co) - k, "]");
    } else {
      k += snprintf(co + k, sizeof(co) - k, "0");
    }
    heap_caps_free(sm);
#else
    k += snprintf(co + k, sizeof(co) - k, "0");
#endif
    snprintf(co + k, sizeof(co) - k, "}");
    return httpd_resp_sendstr(req, co);
  }
  if (httpd_query_key_value(q, "toolmsg", v, sizeof(v)) == ESP_OK) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, gem::lastToolMsg);
  }
  if (httpd_query_key_value(q, "photo", v, sizeof(v)) == ESP_OK) {  // the last photo Oreo saw
    if (!gem::lastPhotoLen) return httpd_resp_sendstr(req, "{\"photo\":0}");
    httpd_resp_set_type(req, "image/jpeg");
    return httpd_resp_send(req, (const char*)gem::lastPhoto, gem::lastPhotoLen);
  }
  if (httpd_query_key_value(q, "ap", v, sizeof(v)) == ESP_OK) {  // test: the robot's hotspot off / on
    esp_wifi_set_mode(atoi(v) ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    return httpd_resp_sendstr(req, atoi(v) ? "{\"ap\":1}" : "{\"ap\":0}");
  }
  if (httpd_query_key_value(q, "dl", v, sizeof(v)) == ESP_OK) {
    if (!strcmp(v, "1")) {
      gem::dlKBps = -1;
      xTaskCreatePinnedToCore(gem::dlTask, "gemDl", 8192, nullptr, 4, nullptr, 0);
    }
    char d[96];
    snprintf(d, sizeof(d), "{\"KBps\":%d,\"bytes\":%d,\"ms\":%d}", (int)gem::dlKBps, (int)gem::dlBytes, (int)gem::dlMs);
    return httpd_resp_sendstr(req, d);
  }
  if (httpd_query_key_value(q, "frames", v, sizeof(v)) == ESP_OK) {
    static char (&fo)[1600] = *(char(*)[1600])heap_caps_calloc(1, sizeof(char) * (1600), MALLOC_CAP_SPIRAM);
    int k = snprintf(fo, sizeof(fo), "{\"readErr\":%d,\"frames\":[", gem::lastReadErr);
    const uint32_t n = gem::flogN, from = n > 32 ? n - 32 : 0;
    for (uint32_t i = from; i < n && k < (int)sizeof(fo) - 40; i++) {
      const gem::FrameLog& f = gem::flog[i & 31];
      k += snprintf(fo + k, sizeof(fo) - k, "%s[%lu,%lu,%u]", i > from ? "," : "", (unsigned long)f.t, (unsigned long)f.len, f.readMs);
    }
    snprintf(fo + k, sizeof(fo) - k, "]}");
    return httpd_resp_sendstr(req, fo);
  }
  static char (&o)[1700] = *(char(*)[1700])heap_caps_calloc(1, sizeof(char) * (1700), MALLOC_CAP_SPIRAM);
  char said[300];
  // (quotes in the transcript would break the JSON)
  strlcpy(said, gem::said, sizeof(said));
  for (char* c = said; *c; c++) if (*c == '"' || *c == '\\' || *c < 32) *c = '\'';
  char err[96];
  strlcpy(err, gem::lastErr, sizeof(err));
  char lastTool[64];
  strlcpy(lastTool, gem::lastTool, sizeof(lastTool));
  for (char* c = lastTool; *c; c++) if (*c == '"' || *c == '\\' || *c < 32) *c = '\'';
  for (char* c = err; *c; c++) if (*c == '"' || *c == '\\' || *c < 32) *c = '\'';
  snprintf(o, sizeof(o),
           "{\"state\":\"%s\",\"ww\":%d,\"wwState\":\"%s\",\"wwWhy\":\"%s\",\"wwCut\":%.2f,\"wwWin\":%d,\"wwNow\":%.2f,\"wwMax\":%.2f,\"wwHits\":%lu,\"wwLvl\":%d,\"wwErr\":\"%s\",\"faceTrack\":%d,\"hasKey\":%d,\"search\":%d,\"legs\":%d,\"tools\":%lu,\"lastTool\":\"%s\",\"preset\":\"%s\",\"voice\":\"%s\",\"pitch\":%.2f,\"fx\":%.2f,\"gain\":%.1f,\"idle\":%d,\"turns\":%lu,"
           "\"connects\":%lu,\"drops\":%lu,\"dropWhy\":\"%s\",\"errors\":%lu,\"bargeIns\":%lu,\"latMs\":%d,\"bufMs\":%d,"
           "\"connMs\":%d,\"underruns\":%lu,\"maxGapMs\":%d,\"maxMsgKB\":%d,\"rx\":%lu,\"tx\":%lu,"
           "\"echo\":%.3f,\"noise\":%.4f,\"inLvl\":%.4f,\"outLvl\":%.4f,\"queuedMs\":%lu,\"intFree\":%u,\"err\":\"%s\","
           "\"stopped\":\"%s\",\"faceAgoMs\":%ld,\"faceX\":%.2f,\"faceDetMs\":%d,\"lookP\":%.1f,\"looks\":%lu,\"lookFails\":%lu,\"lookMs\":%d,\"lookKB\":%d,\"sdHeals\":%lu,\"gate\":%d,\"micBit\":%d,\"micMask\":\"%08lx%08lx\",\"said\":\"%s\"}",
           gem::STATE_NAMES[gem::state], ww::enabled ? 1 : 0, ww::stateName, ww::whyNot, ww::cutoff, ww::window, (float)ww::probNow,
           (float)ww::probMax, (unsigned long)ww::hits, (int)ww::micLvl, ww::err, chatFaceOn ? 1 : 0, gem::key[0] ? 1 : 0, gem::searchOff ? 0 : 1, gem::legsAllowed ? 1 : 0, (unsigned long)gem::toolCalls,
           lastTool, gem::preset, gem::voice, gem::pitch, gem::voiceFx, gem::micGain, gem::idleS,
           (unsigned long)gem::turns, (unsigned long)gem::connects, (unsigned long)gem::drops, gem::dropWhy,
           (unsigned long)gem::errors, (unsigned long)gem::bargeIns, gem::latMs, gem::playDelayMs, gem::connMs,
           (unsigned long)gem::underruns, gem::maxGapMs, gem::maxMsgKB, (unsigned long)gem::rxMsgs,
           (unsigned long)gem::txFrames, (float)gem::coup, (float)gem::noise, (float)gem::inLvl, (float)gem::outLvl,
           (unsigned long)((gem::playW - gem::playR) / 16), heap_caps_get_free_size(MALLOC_CAP_INTERNAL), err,
           gem::stopWhy, chatFaceMs ? (long)(millis() - chatFaceMs) : -1L, (float)chatFaceX, (int)chatFaceDetMs, gem::lookP,
           (unsigned long)gem::looks, (unsigned long)gem::lookFails, gem::lookMs, gem::lookKB,
           (unsigned long)xl::sdHeals, gem::gateOn ? 1 : 0, gem::micBit, (unsigned long)gem::micMaskHi, (unsigned long)gem::micMaskLo, said);
  return httpd_resp_sendstr(req, o);
}
