/*
--------------------------------------------------
    James William Fletcher (github.com/mrbid)
        January 2023
--------------------------------------------------
    C & SDL / OpenGL ES2 / GLSL ES

    sudo apt install libespeak-dev libsdl2-2.0-0 libsdl2-dev
    gcc main.c -I inc -lSDL2 -lGLESv2 -lEGL -pthread -lespeak -Ofast -lm -o MicroPusher
*/

#include <time.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengles2.h>

#include <pthread.h>
#include <espeak/speak_lib.h>

#define NOSSE
#define SEIR_RAND

#include "esAux2.h"
#include "res.h"

#include "assets/scene.h"
#include "assets/coin.h"
#include "assets/pbox.h"
#include "assets/pbow.h"

#define uint GLuint
#define f32 GLfloat
#define forceinline __attribute__((always_inline)) inline

//*************************************
// globals
//*************************************
char appTitle[] = "MicroPusher";
SDL_Window* wnd;
SDL_GLContext glc;
SDL_Surface* s_icon = NULL;
SDL_Cursor* cross_cursor;
SDL_Cursor* beam_cursor;
uint cursor_state = 0;
uint winw = 1024, winh = 768;
f32 aspect;
f32 t = 0.f;
f32 rww, ww, rwh, wh, ww2, wh2;
f32 uw, uh, uw2, uh2; // normalised pixel dpi
f32 touch_margin = 120.f;
f32 mx=0, my=0;
uint md=0, ortho=0;

// render state id's
GLint projection_id;
GLint modelview_id;
GLint position_id;
GLint lightpos_id;
GLint solidcolor_id;
GLint color_id;
GLint opacity_id;
GLint normal_id;

// render state matrices
mat projection;
mat view;
mat model;
mat modelview;

// render state inputs
vec lightpos = {0.f, 10.f, 13.f};

// models
ESModel mdlScene;
ESModel mdlCoin;
ESModel mdlPbox;
ESModel mdlPbow;

// game vars
uint active_coin = 0;
uint inmotion = 0;
f32 goldstrobe = 0.f;

#define COIN_RAD 0.3f
#define COIN_RAD2 0.6f
#define COIN_RAD22 0.36f
typedef struct
{
    f32 x, y;
    signed char type;
} coin;
#define MAX_COINS 130
coin coins[MAX_COINS] = {0};

// stats
f32 coin_stack = 128.f;
f32 PUSH_SPEED = 1.6f;

// metrics
int pid = 0;     // last win present id
uint clicks = 0; // total clicks since game start (well, coin deployments)
uint winc = 0;   // win count

//*************************************
// game functions
//*************************************
forceinline f32 fRandFloat(const f32 min, const f32 max)
{
    return min + randf() * (max-min); 
}

forceinline int fRand(const f32 min, const f32 max)
{
    return (int)((min + randf() * (max-min))+0.5f); 
}

void speak(const char* text)
{
    espeak_Synth(text, strlen(text), 0, 0, 0, espeakCHARS_AUTO, NULL, NULL);
}

char say[32] = {0};
void genWin()
{
    if(say[0] != 0x00){return;}
    const int len = 3+(int)(28.f * randf());
    for(int i = 0; i < len; i++)
        say[i] = 97 + (char)(randf() * 26.f);
    say[len] = '.';
}
void *talk_thread(void *arg)
{
    if(espeak_Initialize(AUDIO_OUTPUT_SYNCH_PLAYBACK, 0, 0, 0) < 0)
    {
        printf("ERROR: speake_Initialize(): failed\n");
        return 0;
    }
    while(1)
    {
        sleep(1);
        if(say[0] != 0x00)
        {
            const size_t slen = strlen(say);
            printf("%s (%li)\n", say, slen);
            speak(say);
            if(slen < 17 && randf() < 0.5f)
            {
                memset(&say, 0x00, sizeof(say));
                genWin();
            }
            else
            {
                memset(&say, 0x00, sizeof(say));
                printf("done.\n");
            }
        }
    }
    return 0;
}

void setActiveCoin()
{
    for(int i=3; i < MAX_COINS; i++)
    {
        if(coins[i].type == -1)
        {
            active_coin = i;
            coins[i].type = 1;
            clicks++;
            return;
        }
    }
}

void takeStack()
{
    if(coin_stack == 0.f)
        return;

    setActiveCoin();
    inmotion = 1;
    if(mx < touch_margin)
    {
        coins[active_coin].x = -1.90433f;
        coins[active_coin].y = -4.54055f;
    }
    else if(mx > ww-touch_margin)
    {
        coins[active_coin].x = 1.90433f;
        coins[active_coin].y = -4.54055f;
    }
    else
    {
        coins[active_coin].x = -1.90433f+(((mx-touch_margin)*rww)*3.80866f);
        coins[active_coin].y = -4.54055f;
    }
}

void injectPbox()
{
    if(inmotion != 0)
        return;
    
    int fc = -1;
    for(int i=0; i < 3; i++)
    {
        if(coins[i].type == -1)
        {
            active_coin = i;
            fc = i;
            coins[i].type = fRand(2.f, 12.f);
            break;
        }
    }

    if(fc != -1)
    {
        coins[active_coin].x = fRandFloat(-1.90433f, 1.90433f);
        coins[active_coin].y = -4.54055f;
        inmotion = 2;
    }
}

int insidePitch(const f32 x, const f32 y, const f32 r)
{
    // off bottom
    if(y < -4.03414f+r)
        return 0;
    
    // first left & right
    if(y < -2.22855f)
    {
        if(x < (-2.22482f - (0.77267f*(fabsf(y+4.03414f) * 0.553835588f))) + r)
            return 0;
        else if(x > (2.22482f + (0.77267f*(fabsf(y+4.03414f) * 0.553835588f))) - r)
            return 0;
    }
    else if(y < -0.292027f) // second left & right
    {
        if(x < (-2.99749f - (0.41114f*(fabsf(y+2.22855f) * 0.516389426f))) + r)
            return 0;
        else if(x > (2.99749f + (0.41114f*(fabsf(y+2.22855f) * 0.516389426f))) - r)
            return 0;
    }
    else if(y < 1.64f) // third left & right
    {
        if(x < -3.40863f + r)
            return 0;
        else if(x > 3.40863f - r)
            return 0;
    }

    return 1;
}

int collision(int ci)
{
    for(int i=0; i < MAX_COINS; i++)
    {
        if(i == ci || coins[i].type == -1){continue;}
        const f32 xm = (coins[i].x - coins[ci].x);
        const f32 ym = (coins[i].y - coins[ci].y);
        if(sqrtps(xm*xm + ym*ym) < COIN_RAD2)
            return 1;
    }
    return 0;
}

uint stepCollisions()
{
    uint was_collision = 0;
    for(int i=0; i < MAX_COINS; i++)
    {
        if(coins[i].type == -1){continue;}
        for(int j=0; j < MAX_COINS; j++)
        {
            if(i == j || coins[j].type == -1 || j == active_coin){continue;}
            f32 xm = (coins[i].x - coins[j].x);
            xm += fRandFloat(-0.01f, 0.01f); // add some random offset to our unit vector, very subtle but works so well!
            const f32 ym = (coins[i].y - coins[j].y);
            f32 d = xm*xm + ym*ym;
            if(d < COIN_RAD22)
            {
                d = sqrtps(d);
                const f32 cr = COIN_RAD2;
                //xm += fRandFloat(-0.01f, 0.01f); // add some random offset to our unit vector (not as good)
                //d = sqrtps(xm*xm + ym*ym); // recompute d
                const float len = 1.f/d;
                const f32 uy = (ym * len);
                if(uy > 0.f){continue;} // best hack ever to massively simplify
                const f32 m = d-cr;
                coins[j].x += (xm * len) * m;
                coins[j].y += uy * m;

                // first left & right
                if(coins[j].y < -2.22855f)
                {
                    const f32 fl = (-2.22482f - (0.77267f*(fabsf(coins[j].y+4.03414f) * 0.553835588f))) + COIN_RAD;
                    if(coins[j].x < fl)
                    {
                        coins[j].x = fl;
                    }
                    else
                    {
                        const f32 fr = ( 2.22482f + (0.77267f*(fabsf(coins[j].y+4.03414f) * 0.553835588f))) - COIN_RAD;
                        if(coins[j].x > fr)
                            coins[j].x = fr;
                    }
                }
                else if(coins[j].y < -0.292027f) // second left & right
                {
                    const f32 fl = (-2.99749f - (0.41114f*(fabsf(coins[j].y+2.22855f) * 0.516389426f))) + COIN_RAD;
                    if(coins[j].x < fl)
                    {
                        coins[j].x = fl;
                    }
                    else
                    {
                        const f32 fr = (2.99749f + (0.41114f*(fabsf(coins[j].y+2.22855f) * 0.516389426f))) - COIN_RAD;
                        if(coins[j].x > fr)
                            coins[j].x = fr;
                    }
                }
                else if(coins[j].y < 1.45439f) // third left & right
                {
                    const f32 fl = -3.40863f + COIN_RAD;
                    if(coins[j].x < fl)
                    {
                        coins[j].x = fl;
                    }
                    else
                    {
                        const f32 fr = 3.40863f - COIN_RAD;
                        if(coins[j].x > fr)
                            coins[j].x = fr;
                    }
                }
                else if(coins[j].y < 2.58397f) // first house goal
                {
                    const f32 fl = (-3.40863f + (0.41113f*(fabsf(coins[j].y-1.45439f) * 0.885284796f)));
                    if(coins[j].x < fl)
                    {
                        coins[j].type = -1;
                    }
                    else
                    {
                        const f32 fr = (3.40863f - (0.41113f*(fabsf(coins[j].y-1.45439f) * 0.885284796f)));
                        if(coins[j].x > fr)
                        {
                            coins[j].type = -1;
                        }
                    }
                }
                else if(coins[j].y < 3.70642f) // second house goal
                {
                    const f32 fl = (-2.9975f + (1.34581f*(fabsf(coins[j].y-2.58397f) * 0.890908281f)));
                    if(coins[j].x < fl)
                    {
                        coins[j].type = -1;
                    }
                    else
                    {
                        const f32 fr = (2.9975f - (1.34581f*(fabsf(coins[j].y-2.58397f) * 0.890908281f)));
                        if(coins[j].x > fr)
                        {
                            coins[j].type = -1;
                        }
                    }
                }
                else if(coins[j].y < 4.10583f) // silver goal
                {
                    const f32 fl = (-1.65169f + (1.067374f*(fabsf(coins[j].y-3.70642f) * 2.503692947f)));
                    if(coins[j].x < fl)
                    {
                        if(j < 3 && goldstrobe == 1.f)
                        {
                            coins[j].x = fl; // win one present at a time
                        }
                        else
                        {
                            if(j < 3){goldstrobe = 1.f; pid=coins[j].type; winc++; genWin();}
                            coins[j].type = -1;
                        }
                    }
                    else
                    {
                        const f32 fr = (1.65169f - (1.067374f*(fabsf(coins[j].y-3.70642f) * 2.503692947f)));
                        if(coins[j].x > fr)
                        {
                            if(j < 3 && goldstrobe == 1.f)
                            {
                                coins[j].x = fr; // win one present at a time
                            }
                            else
                            {
                                if(j < 3){goldstrobe = 1.f; pid=coins[j].type; winc++; genWin();}
                                coins[j].type = -1;
                            }
                        }
                    }
                }
                else if(coins[j].y >= 4.31457f)
                {
                    if(coins[j].x >= -0.584316f && coins[j].x <= 0.584316f) // gold goal
                    {
                        if(j < 3 && goldstrobe == 1.f)
                        {
                            coins[j].y = 4.31457f; // win one present at a time
                        }
                        else
                        {
                            if(j < 3){goldstrobe = 1.f; pid=coins[j].type; winc++; genWin();}
                            coins[j].type = -1;
                        }
                    }
                    else
                    {
                        if(j < 3 && goldstrobe == 1.f)
                        {
                            coins[j].y = 4.31457f; // win one present at a time
                        }
                        else
                        {
                            if(j < 3){goldstrobe = 1.f; pid=coins[j].type; winc++; genWin();}
                            coins[j].type = -1;
                        }
                    }
                }

                
                was_collision++;
            }
        }
    }
    return was_collision;
}

void newGame()
{
    // seed randoms
    srandf(time(0));
    srand(time(0));

    // defaults
    coin_stack = 128.f;
    active_coin = 0;
    inmotion = 0;
    for(int i=0; i < MAX_COINS; i++)
        coins[i].type = -1;

    // presents
    for(int i=0; i < 3; i++)
    {
        coins[i].type = fRand(2.f, 12.f);
        coins[i].x = fRandFloat(-3.40863f, 3.40863f);
        coins[i].y = fRandFloat(-4.03414f, 1.45439f-COIN_RAD);
        while(insidePitch(coins[i].x, coins[i].y, COIN_RAD) == 0 || collision(i) == 1)
        {
            coins[i].x = fRandFloat(-3.40863f, 3.40863f);
            coins[i].y = fRandFloat(-4.03414f, 1.45439f-COIN_RAD);
        }
    }

    // coins
    int lt = SDL_GetTicks();
    for(int i=3; i < MAX_COINS; i++)
    {
        coins[i].x = fRandFloat(-3.40863f, 3.40863f);
        coins[i].y = fRandFloat(-4.03414f, 1.45439f-COIN_RAD);
        uint tl = 0;
        while(insidePitch(coins[i].x, coins[i].y, COIN_RAD) == 0 || collision(i) == 1)
        {
            coins[i].x = fRandFloat(-3.40863f, 3.40863f);
            coins[i].y = fRandFloat(-4.03414f, 1.45439f-COIN_RAD);
            if(SDL_GetTicks()-lt > 33){tl=1;break;} // 33ms timeout
        }

        if(tl==1){break;}
        coins[i].type = 1;
    }
}

//*************************************
// render functions
//*************************************
forceinline void modelBind1(const ESModel* mdl)
{
    glBindBuffer(GL_ARRAY_BUFFER, mdl->vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdl->nid);
    glVertexAttribPointer(normal_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(normal_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->iid);
}

forceinline void modelBind3(const ESModel* mdl)
{
    glBindBuffer(GL_ARRAY_BUFFER, mdl->cid);
    glVertexAttribPointer(color_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(color_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdl->vid);
    glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(position_id);

    glBindBuffer(GL_ARRAY_BUFFER, mdl->nid);
    glVertexAttribPointer(normal_id, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(normal_id);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl->iid);
}

//*************************************
// emscripten/gl functions
//*************************************
void doPerspective()
{
    glViewport(0, 0, winw, winh);

    ww = (f32)winw;
    wh = (f32)winh;
    if(ortho == 1){touch_margin = ww*0.3076923192f;}
    else{touch_margin = ww*0.2058590651f;}
    rww = 1.f/(ww-touch_margin*2.f);
    rwh = 1.f/wh;
    ww2 = ww/2.f;
    wh2 = wh/2.f;
    uw = aspect/ww;
    uh = 1.f/wh;
    uw2 = aspect/ww2;
    uh2 = 1.f/wh2;

    mIdent(&projection);

    if(ortho == 1)
        mOrtho(&projection, -5.0f, 5.0f, -3.2f, 3.4f, 0.01f, 320.f);
    else
    {
        if(winw > winh)
            aspect = ww / wh;
        else
            aspect = wh / ww;
        mPerspective(&projection, 30.0f, aspect, 0.1f, 320.f);
    }

}

//*************************************
// update & render
//*************************************
void main_loop()
{
//*************************************
// time delta for interpolation
//*************************************
    static f32 lt = 0;
    t = ((f32)SDL_GetTicks())*0.001f;
    const f32 dt = t-lt;
    lt = t;

//*************************************
// input handling
//*************************************
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_MOUSEMOTION:
            {
                mx = (f32)event.motion.x;
                my = (f32)event.motion.y;
            }
            break;

            case SDL_MOUSEBUTTONDOWN:
            {
                if(inmotion == 0 && event.button.button == SDL_BUTTON_LEFT)
                {
                    takeStack();
                    md = 1;
                }

                if(event.button.button == SDL_BUTTON_RIGHT)
                {
                    static uint cs = 1;
                    cs = 1 - cs;
                    if(cs == 0)
                        SDL_ShowCursor(0);
                    else
                        SDL_ShowCursor(1);
                }
            }
            break;

            case SDL_MOUSEBUTTONUP:
            {
                if(event.button.button == SDL_BUTTON_LEFT)
                    md = 0;
            }
            break;

            case SDL_KEYDOWN:
            {
                if(event.key.keysym.sym == SDLK_c)
                {
                    ortho = 1 - ortho;
                    doPerspective();
                    
                }
                else if(event.key.keysym.sym == SDLK_t)
                    genWin();
            }
            break;

            case SDL_WINDOWEVENT:
            {
                if(event.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    winw = event.window.data1;
                    winh = event.window.data2;
                    doPerspective();
                }
            }
            break;

            case SDL_QUIT:
            {
                SDL_GL_DeleteContext(glc);
                SDL_FreeSurface(s_icon);
                SDL_FreeCursor(cross_cursor);
                SDL_FreeCursor(beam_cursor);
                SDL_DestroyWindow(wnd);
                SDL_Quit();
                exit(0);
            }
            break;
        }
    }
    
//*************************************
// begin render
//*************************************
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

//*************************************
// main render
//*************************************

    // camera
    mIdent(&view);
    mTranslate(&view, 0.f, 0.f, -13.f);
    if(ortho == 1)
        mRotY(&view, 50.f*DEG2RAD);
    else
        mRotY(&view, 62.f*DEG2RAD);

    injectPbox();
    
    // prep scene for rendering
    shadeLambert3(&position_id, &projection_id, &modelview_id, &lightpos_id, &normal_id, &color_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (f32*) &projection.m[0][0]);
    glUniform3f(lightpos_id, lightpos.x, lightpos.y, lightpos.z);
    glUniform1f(opacity_id, 1.0f);

    // maticies
    glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &view.m[0][0]);

    // render
    modelBind3(&mdlScene);
    glDrawElements(GL_TRIANGLES, scene_numind, GL_UNSIGNED_SHORT, 0);

    // prep pieces for rendering
    shadeLambert1(&position_id, &projection_id, &modelview_id, &lightpos_id, &normal_id, &color_id, &opacity_id);
    glUniformMatrix4fv(projection_id, 1, GL_FALSE, (f32*) &projection.m[0][0]);
    glUniform3f(lightpos_id, lightpos.x, lightpos.y, lightpos.z);
    glUniform1f(opacity_id, 1.0f);

    // coin
    modelBind1(&mdlCoin);
    //glUniform3f(color_id, 0.76471f, 0.63529f, 0.18824f);
    glUniform3f(color_id, 0.68235f, 0.70196f, 0.72941f);

    // cursor
    if(cursor_state == 0 && mx < touch_margin-1.f)
    {
        SDL_SetCursor(beam_cursor);
        cursor_state = 1;
    }
    else if(cursor_state == 0 && mx > ww-touch_margin+1.f)
    {
        SDL_SetCursor(beam_cursor);
        cursor_state = 1;
    }
    else if(cursor_state == 1 && mx > touch_margin && mx < ww-touch_margin)
    {
        SDL_SetCursor(cross_cursor);
        cursor_state = 0;
    }

    // targeting coin
    if(coin_stack > 0.f)
    {
        if(inmotion == 0)
        {
            mIdent(&model);

            if(mx < touch_margin)
                mTranslate(&model, -1.90433f, -4.54055f, 0);
            else if(mx > ww-touch_margin)
                mTranslate(&model, 1.90433f, -4.54055f, 0);
            else
                mTranslate(&model, -1.90433f+(((mx-touch_margin)*rww)*3.80866f), -4.54055f, 0);

            mMul(&modelview, &model, &view);
            glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
            glDrawElements(GL_TRIANGLES, coin_numind, GL_UNSIGNED_BYTE, 0);
        }
    }

    // do motion
    if(inmotion > 0)
    {
        if(coins[active_coin].y < -3.73414f)
        {
            coins[active_coin].y += PUSH_SPEED * dt;
            if(PUSH_SPEED > 1.6f)
            {
                for(int i=0; i < 6; i++) // six seems enough
                    stepCollisions();
            }
            else
                stepCollisions();
        }
        else
            inmotion = 0;
    }

    // pitch coins
    for(int i=3; i < MAX_COINS; i++)
    {
        if(coins[i].type == -1)
            continue;

        mIdent(&model);
        mTranslate(&model, coins[i].x, coins[i].y, 0.f);
        mMul(&modelview, &model, &view);
        glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
        glDrawElements(GL_TRIANGLES, coin_numind, GL_UNSIGNED_BYTE, 0);
    }

    // stacks
    static uint gst = 0;
    if(goldstrobe == 1.f)
        gst = 1;
    else if(goldstrobe == 0.f && gst == 1)
        gst = 0;

    f32 half_coin_stack1 = 0.f;
    if(coin_stack > 64.f)
    {
        half_coin_stack1 = coin_stack-64.f;
        for(f32 i = 0.f; i < half_coin_stack1; i += 1.f)
        {
            mIdent(&model);
            if(ortho == 0)
                mTranslate(&model, 2.62939f, -4.54055f, 0.0406f*i);
            else
                mTranslate(&model, 4.62939f, -4.54055f, 0.0406f*i);

            if(goldstrobe > 0.f)
            {
                if(goldstrobe > i-3.f && goldstrobe < i+3.f)
                    glUniform3f(color_id, 0.76471f, 0.63529f, 0.18824f);
                else
                    glUniform3f(color_id, 0.68235f, 0.70196f, 0.72941f);

                if(goldstrobe > half_coin_stack1)
                    goldstrobe = 0.f;

                static uint lt = 0;
                if(SDL_GetTicks() > lt)
                {
                    goldstrobe += 1.f;
                    lt = SDL_GetTicks()+11;
                }
            }

            mMul(&modelview, &model, &view);
            glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
            glDrawElements(GL_TRIANGLES, coin_numind, GL_UNSIGNED_BYTE, 0);
        }
    }
    if(coin_stack > 0.f)
    {
        const f32 half_coin_stack2 = coin_stack - half_coin_stack1;
        for(f32 i = 0.f; i < half_coin_stack2; i += 1.f)
        {
            mIdent(&model);
            if(ortho == 0)
                mTranslate(&model, -2.62939f, -4.54055f, 0.0406f*i);
            else
                mTranslate(&model, -4.62939f, -4.54055f, 0.0406f*i);

            if(goldstrobe > 0.f)
            {
                if(goldstrobe > i-3.f && goldstrobe < i+3.f)
                    glUniform3f(color_id, 0.76471f, 0.63529f, 0.18824f);
                else
                    glUniform3f(color_id, 0.68235f, 0.70196f, 0.72941f);

                if(goldstrobe > half_coin_stack1)
                    goldstrobe = 0.f;

                static uint lt = 0;
                if(SDL_GetTicks() > lt)
                {
                    goldstrobe += 1.f;
                    lt = SDL_GetTicks()+11;
                }
            }

            mMul(&modelview, &model, &view);
            glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);
            glDrawElements(GL_TRIANGLES, coin_numind, GL_UNSIGNED_BYTE, 0);
        }
    }

    // presents
    for(int i=0; i < 3; i++)
    {
        if(coins[i].type == -1)
            continue;

        mIdent(&model);
        mTranslate(&model, coins[i].x, coins[i].y, 0.f);
        mMul(&modelview, &model, &view);
        glUniformMatrix4fv(modelview_id, 1, GL_FALSE, (f32*) &modelview.m[0][0]);

        // bronze; 0.80392f, 0.49804f, 0.19608f
        // maroon; 0.50196f, 0.00000f, 0.00000f
        // silver; 0.68235f, 0.70196f, 0.72941f
        // gold; 0.76471f, 0.63529f, 0.18824f
        // purple; 0.31765f, 0.03137f, 0.49412f
        // pink; 0.69804f, 0.03529f, 0.73725f
        // sky blue; 0.00000f, 0.67843f, 0.70588f
        // turquoise; 0.18824f, 0.83529f, 0.78431f
        // cobalt; 0.00000f, 0.27843f, 0.67059f

        if(coins[i].type == 2)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.50196f, 0.00000f, 0.00000f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.68235f, 0.70196f, 0.72941f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 3)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.50196f, 0.00000f, 0.00000f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.76471f, 0.63529f, 0.18824f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 4)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.31765f, 0.03137f, 0.49412f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.68235f, 0.70196f, 0.72941f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 5)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.31765f, 0.03137f, 0.49412f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.76471f, 0.63529f, 0.18824f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 6)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.69804f, 0.03529f, 0.73725f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.68235f, 0.70196f, 0.72941f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 7)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.69804f, 0.03529f, 0.73725f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.76471f, 0.63529f, 0.18824f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 8)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.18824f, 0.83529f, 0.78431f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 1.f, 1.f, 1.f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 9)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.31765f, 0.03137f, 0.49412f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.00000f, 0.67843f, 0.70588f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 10)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 1.f, 1.f, 1.f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.00000f, 0.27843f, 0.67059f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 11)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.00000f, 0.27843f, 0.67059f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.68235f, 0.70196f, 0.72941f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
        else if(coins[i].type == 12)
        {
            modelBind1(&mdlPbox);
            glUniform3f(color_id, 0.00000f, 0.27843f, 0.67059f);
            glDrawElements(GL_TRIANGLES, pbox_numind, GL_UNSIGNED_SHORT, 0);
            modelBind1(&mdlPbow);
            glUniform3f(color_id, 0.76471f, 0.63529f, 0.18824f);
            glDrawElements(GL_TRIANGLES, pbow_numind, GL_UNSIGNED_BYTE, 0);
        }
    }

//*************************************
// swap buffers / display render
//*************************************
    SDL_GL_SwapWindow(wnd);
}

//*************************************
// Process Entry Point
//*************************************
SDL_Surface* surfaceFromData(const Uint32* data, Uint32 w, Uint32 h)
{
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    memcpy(s->pixels, data, s->pitch*h);
    return s;
}
void printAttrib(SDL_GLattr attr, char* name)
{
    int i;
    SDL_GL_GetAttribute(attr, &i);
    printf("%s: %i\n", name, i);
}
int main(int argc, char** argv)
{
//*************************************
// argv and info text
//*************************************

    // allow custom msaa level
    int msaa = 16;
    if(argc >= 2){msaa = atoi(argv[1]);}

    // help
    printf("----\n");
    printf("Pusha.one\n");
    printf("----\n");
    printf("James William Fletcher (github.com/mrbid)\n");
    printf("----\n");
    printf("Argv(2): msaa, speed\n");
    printf("e.g; ./uc 16 1.6\n");
    printf("----\n");
    printf("Left Click = Release coin\n");
    printf("Right Click = Show/hide cursor\n");
    printf("C = Orthographic/Perspective\n");
    printf("----\n");
    printAttrib(SDL_GL_DOUBLEBUFFER, "GL_DOUBLEBUFFER");
    printAttrib(SDL_GL_DEPTH_SIZE, "GL_DEPTH_SIZE");
    printAttrib(SDL_GL_RED_SIZE, "GL_RED_SIZE");
    printAttrib(SDL_GL_GREEN_SIZE, "GL_GREEN_SIZE");
    printAttrib(SDL_GL_BLUE_SIZE, "GL_BLUE_SIZE");
    printAttrib(SDL_GL_ALPHA_SIZE, "GL_ALPHA_SIZE");
    printAttrib(SDL_GL_BUFFER_SIZE, "GL_BUFFER_SIZE");
    printAttrib(SDL_GL_DOUBLEBUFFER, "GL_DOUBLEBUFFER");
    printAttrib(SDL_GL_DEPTH_SIZE, "GL_DEPTH_SIZE");
    printAttrib(SDL_GL_STENCIL_SIZE, "GL_STENCIL_SIZE");
    printAttrib(SDL_GL_ACCUM_RED_SIZE, "GL_ACCUM_RED_SIZE");
    printAttrib(SDL_GL_ACCUM_GREEN_SIZE, "GL_ACCUM_GREEN_SIZE");
    printAttrib(SDL_GL_ACCUM_BLUE_SIZE, "GL_ACCUM_BLUE_SIZE");
    printAttrib(SDL_GL_ACCUM_ALPHA_SIZE, "GL_ACCUM_ALPHA_SIZE");
    printAttrib(SDL_GL_STEREO, "GL_STEREO");
    printAttrib(SDL_GL_MULTISAMPLEBUFFERS, "GL_MULTISAMPLEBUFFERS");
    printAttrib(SDL_GL_MULTISAMPLESAMPLES, "GL_MULTISAMPLESAMPLES");
    printAttrib(SDL_GL_ACCELERATED_VISUAL, "GL_ACCELERATED_VISUAL");
    printAttrib(SDL_GL_RETAINED_BACKING, "GL_RETAINED_BACKING");
    printAttrib(SDL_GL_CONTEXT_MAJOR_VERSION, "GL_CONTEXT_MAJOR_VERSION");
    printAttrib(SDL_GL_CONTEXT_MINOR_VERSION, "GL_CONTEXT_MINOR_VERSION");
    printAttrib(SDL_GL_CONTEXT_FLAGS, "GL_CONTEXT_FLAGS");
    printAttrib(SDL_GL_CONTEXT_PROFILE_MASK, "GL_CONTEXT_PROFILE_MASK");
    printAttrib(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, "GL_SHARE_WITH_CURRENT_CONTEXT");
    printAttrib(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, "GL_FRAMEBUFFER_SRGB_CAPABLE");
    printAttrib(SDL_GL_CONTEXT_RELEASE_BEHAVIOR, "GL_CONTEXT_RELEASE_BEHAVIOR");
    printAttrib(SDL_GL_CONTEXT_EGL, "GL_CONTEXT_EGL");
    printf("----\n");
    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    printf("Compiled against SDL version %u.%u.%u.\n", compiled.major, compiled.minor, compiled.patch);
    printf("Linked against SDL version %u.%u.%u.\n", linked.major, linked.minor, linked.patch);
    printf("----\n");

//*************************************
// setup render context / window
//*************************************
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) < 0) //SDL_INIT_AUDIO
    {
        printf("ERROR: SDL_Init(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 16);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    wnd = SDL_CreateWindow(appTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winw, winh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if(wnd == NULL)
    {
        printf("ERROR: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    glc = SDL_GL_CreateContext(wnd);
    if(glc == NULL)
    {
        printf("ERROR: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return 1;
    }

    // talk thread
    pthread_t tid;
    if(pthread_create(&tid, NULL, talk_thread, NULL) != 0)
    {
        printf("ERROR: pthread_create(): failed\n");
        return 1;
    }
    pthread_detach(tid);

    // set cursors
    cross_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    beam_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    SDL_SetCursor(cross_cursor);

    // set icon
    s_icon = surfaceFromData((Uint32*)&icon_image.pixel_data[0], 16, 16);
    SDL_SetWindowIcon(wnd, s_icon);

    // set game push speed
    PUSH_SPEED = 1.6f;
    if(argc >= 3)
    {
        PUSH_SPEED = atof(argv[2]);
        if(PUSH_SPEED > 32.f){PUSH_SPEED = 32.f;}
        char titlestr[256];
        sprintf(titlestr, "%s [%.1f]", appTitle, PUSH_SPEED);
        SDL_SetWindowTitle(wnd, titlestr);
    }

    // new game
    newGame();

//*************************************
// bind vertex and index buffers
//*************************************

    // ***** BIND SCENE *****
    esBind(GL_ARRAY_BUFFER, &mdlScene.cid, scene_colors, sizeof(scene_colors), GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlScene.vid, scene_vertices, sizeof(scene_vertices), GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlScene.nid, scene_normals, sizeof(scene_normals), GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlScene.iid, scene_indices, sizeof(scene_indices), GL_STATIC_DRAW);

    // ***** BIND COIN *****
    esBind(GL_ARRAY_BUFFER, &mdlCoin.vid, coin_vertices, sizeof(coin_vertices), GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlCoin.nid, coin_normals, sizeof(coin_normals), GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlCoin.iid, coin_indices, sizeof(coin_indices), GL_STATIC_DRAW);

    // ***** BIND PBOX *****
    esBind(GL_ARRAY_BUFFER, &mdlPbox.vid, pbox_vertices, sizeof(pbox_vertices), GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlPbox.nid, pbox_normals, sizeof(pbox_normals), GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlPbox.iid, pbox_indices, sizeof(pbox_indices), GL_STATIC_DRAW);

    // ***** BIND PBOW *****
    esBind(GL_ARRAY_BUFFER, &mdlPbow.vid, pbow_vertices, sizeof(pbow_vertices), GL_STATIC_DRAW);
    esBind(GL_ARRAY_BUFFER, &mdlPbow.nid, pbow_normals, sizeof(pbow_normals), GL_STATIC_DRAW);
    esBind(GL_ELEMENT_ARRAY_BUFFER, &mdlPbow.iid, pbow_indices, sizeof(pbow_indices), GL_STATIC_DRAW);

//*************************************
// projection
//*************************************

    doPerspective();

//*************************************
// compile & link shader program
//*************************************

    makeLambert1();
    makeLambert3();

//*************************************
// configure render options
//*************************************

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.52941f, 0.80784f, 0.92157f, 0.0f);

//*************************************
// execute update / render loop
//*************************************
    
    // event loop
    while(1){main_loop();}
    return 0;

}

