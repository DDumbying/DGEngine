this is me trying to learn about the game engine and make some kind of progress by building one..

this doesn't mean that i will diffenetly build one, i may build one or maybe not, this depends on a lot of things :

- my time
- the dificalty
- idk it's just yes or no but insha' allah will make it.

first of all we need to know what is even this thing and what it's about and why we need to make it from the beginning.

## Why i want to build a Game Engine?

- because it's cool to have your own game engine.
- sometimes it's because the value of what this will teach me. (basically the learning process)

but this is for me, but what about if you want to build your own one?

this actually depend on some real reasons not these silly ones that i gave to myself.

here's a few **real/good** reasons why you might want to:

- **Novel Tech**: You want to make a game that uses a piece of novel tech that no other engines out there currently support, or can't be easily made to support in thier current state.
this can mean some kind of massive-scale simulation that requires some to-the-metal coding to make performant (factorio) or some custom thing that doesn't fit into any existing modls (Noita, Migakure), or whishing to target an odd piece of hardware that current engines don't support (Playdate),
or any number of other things really. It's a good reason to make your own engine, cause there isn't any other options in cases like this.

- **Specialization**: You want to optimize your workflow for the kind of games you make. You don't need all the features included in a commercial game engine, and you can make your assets pipeline / level editor /whatever way smoother to use when considering your specific use cases instead of needing it to be general purpose.
Specializating it and catering it twards your exact use case, you should rething why you're making an engine in the first place.

- **Independence**: You don't want to be dependent on some else's technology **in the long term**. The incentives and values of a company like *Unity* or *Epic* are not always going to align with your own, and you want control over your own tech, the ability to fix bugs yourself instead of "*waiting and hoping*", and comfort knowing thaht an update won't completely break your current project.
You are willing to eat the cost of developing your own tech because in the long g tun it's good to not have to constantly change stuff depending of the whhims of gaint companies.

- **Curiosity/Learning**: You're just curious about how it works and why othehr engines have made certain decisions they did. This is a great reason, one of the best reasons actually, to make your own game engine. *I really fit with this one*

btw i reference these reasons from this [blog](https://medium.com/geekculture/how-to-make-your-own-game-engine-and-why-ddf0acbc5f3).

There was some bad reasons but i will make it up to you, if you want to know them, because i see that my own experience doesn't (*till now at least*) doesn't have the ability to give a bad ones.. i am just descovering while reading.

so after we know the Why? let's know the What!

## What is even the Game Engine?

The game engine is *a software framework that provides the core tools and systems needed to build a video games*, such as rendering engines (2D/3D), physics simulations, sound, and animation, It
acts as a foundation, allowing developers to create games efficiently without building fundamental systems from scratch.

so after knowing all that, what also make the engine a ENGINE!

## What is expected from a game engine?

we could say that the convintion of the engine could be small, but this is for the engines that doens't aim to be a real production engines, because let's be realistic, there are things that are expected from most big enough game engines, unless they are exteremely minimalistic or specialized by desin.. let's name a few:

- Being able to **program** the logic of your game (duh)
- Creating a **window** to display stuff
- Running a **game loop** that processes various events
- Handling **user input**, like the *mouse*, *keyboard*, *joystick* or *touchbar*.
- Displaying **graphics** of some sort, be it 2D sprite-based or something 3D
- Producing **audio** like background music and sound effects
- Managing **assets** like images, models, audio, prefabs, or full game levels
- Simulating **physics**, be it 2D or 3D, position-based or impulse-based or force-based, hard or soft
- Supporting **scripting** for rapid prototyping or easy modding
- Supporting **networking** for multiplayer, automatically downloading game updates or patches, or enabling downloadable user-generated content
- Implementing **AI** for your game's enemies or NPC's
- Implementing **UI** for your game's glorious interfaces
- Imposing an **architecture** that connects all these parts in a uniform way
- Providing **tools** that help you use the engine
- Taking care of **distribution** of games made with this engine, ideally on several platforms, ideally by a single button click

Actually these things are the main ones (*or the really specific ones that fits with me*).

let's now spread them one by one.

### programming

While reading some blogs i heard about a feature that most engines support for programming that is *Visual Scripting*, However i conclused that supporting visual scripting in my Engine requires writing specialized tools, which noticeably complicates the engine development. And, in my opinion, code is still the most general and verstile way to implement any type of logic.

So you might also get a way with supporting scripting in some languages other than whaht the engine is written in (e.g. a `C++` engine with `lua` scripting and no direct `C++` interface), but if your're already writing an engine, it's probably easier to simply provide a native API (i.e. in the language theh engine itself is written with).

However, writing an engine still requires programming, no shortcuts here, so the best language for writing an engine doesn't matter alot, um just picking the most language i am familiar with anyway.

### Window

Most games need a window to run in. Even web games aren't exception: the window may be the whole web page, or a certain canvas element the game is running in, etc. Probably the only exeption are terminal-based games which use the terminal for all inputs and outputs.

*Choosing the "Programming" way, and building the window the first things we need to make sure that we decide before doing the Next important thing*

### Game Loop

That's whhat almost all ggames and game engines have in common though the loop itself might be hidden quite deep inside the engine. On a higher level, a game's code looks like:

```
int main()
{
    createWindow();
    setupStuff();
    while (gameIsRunning)
    {
        processInput();
        simulateStuff();
        drawFrame();
    }
}
```

This `while (gameIsRunning)` loop is the core game loop. It is what makes your game run, what adds the time dimension to your game. Without it, the `main` function would simply return and the game would close.

Again, there are many possibilities in exposing the loop in the engine's API. It may be completely explicit, i.e. the engine's user writes while `(engine::isRunning())` somewhere in their `main` function. It may be hidden inside the engine, e.g. in some `engine::run()` function, which in turn calls some functions that the user provides. Maybe the user subclasses an `engine::application` class and has to override some `application.update()` method that gets called from the `application.run()` method.

Whaterver you choose, you need a game loop.


