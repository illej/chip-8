#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#define ASSERT(EXPR) if (!(EXPR)) { *(volatile int *) 0 = 0; }
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define RENDER_BUFFER_WIDTH 64
#define RENDER_BUFFER_HEIGHT 32
#define RENDER_SCALE 5

#define VX(__opcode) ((__opcode & 0x0F00) >> 8)
#define VY(__opcode) ((__opcode & 0x00F0) >> 4)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

struct emulator
{
    // SDL
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    // chip8

    // 4KB of memory
    // [0   -  511] 0x000 - 0x1FF: Chip 8 interpreter
    // [80  -  160] 0x050 - 0x0A0: Used for the built-in 4x5 pixel font set (0-F)
    // [512 - 4095] 0x200 - 0xFFF: Program ROM and work RAM
    u8 memory[4096];
    u32 rom_size;

    // 15 registers (V0 - VE)
    // 16th register (VF): 'carry flag' register. 1 when there is a carry, otherwise 0.
    u8 registers[16];

    // 0x000 - 0xFFF
    // I
    u16 index_register;
    // pc
    // tracks the current opcode in memory
    u16 program_counter;

    // black and white
    // 2048 pixels
    // pixel state: 1 / 0
    u32 gfx[RENDER_BUFFER_WIDTH * RENDER_BUFFER_HEIGHT];
    bool draw_flag;

    // timers
    // tick at 60hz
    u8 delay_timer;
    u8 sound_timer;

    // stack
    u16 stack[16];
    u16 stack_ptr;

    // hex-based keypad (0x0 - 0xF)
    u8 keys[16];
};


static bool g__running = true;
static u8 font_set[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, //0
    0x20, 0x60, 0x20, 0x20, 0x70, //1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, //2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, //3
    0x90, 0x90, 0xF0, 0x10, 0x10, //4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, //5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, //6
    0xF0, 0x10, 0x20, 0x40, 0x40, //7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, //8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, //9
    0xF0, 0x90, 0xF0, 0x90, 0x90, //A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, //B
    0xF0, 0x80, 0x80, 0x80, 0xF0, //C
    0xE0, 0x90, 0x90, 0x90, 0xE0, //D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, //E
    0xF0, 0x80, 0xF0, 0x80, 0x80  //F
};


static void
init (struct emulator *emu, int window_width, int window_height, int texture_width, int texture_height)
{
    SDL_Init (SDL_INIT_VIDEO);
    emu->window = SDL_CreateWindow ("chip8 emulator",
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    window_width,
                                    window_height,
                                    SDL_WINDOW_SHOWN);
    emu->renderer = SDL_CreateRenderer (emu->window, -1, SDL_RENDERER_ACCELERATED);
    emu->texture = SDL_CreateTexture (emu->renderer,
                                      SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      texture_width,
                                      texture_height);

    // ROM loads into 0x200
    emu->program_counter = 0x200;

    // fontset loads into 0x50 - 0xA0 (80 - 160)
    for (u32 i = 0; i < 80; i++)
    {
        emu->memory[0x50 + i] = font_set[i];
    }

    ASSERT (RENDER_BUFFER_WIDTH * RENDER_BUFFER_HEIGHT == 2048);
    ASSERT (emu->window && emu->renderer && emu->texture);
}

static void
deinit (struct emulator *emu)
{
    SDL_DestroyTexture (emu->texture);
    SDL_DestroyRenderer (emu->renderer);
    SDL_DestroyWindow (emu->window);
    SDL_Quit ();
}

static bool
load_rom (struct emulator *emu, char *file)
{
    bool ok = false;
    FILE *fp = fopen (file, "rb");

    if (fp)
    {
        fseek (fp, 0, SEEK_END);
        size_t len = ftell (fp);
        fseek (fp, 0, SEEK_SET);

        u32 available_memory = 0xFFF - 0x200;
        if (len <= available_memory)
        {
            u8 *mem = &emu->memory[0x200];
            if (fread (mem, 1, len, fp) == len)
            {
                printf ("rom binary [%zu B]:\n", len);

                int cols = 0;
                for (u32 i = 0; i < len; i++)
                {
                    printf (" %02X", mem[i]);
                    if (++cols == 16)
                    {
                        printf ("\n");
                        cols = 0;
                    }
                }
                printf ("\n");

                emu->rom_size = len;
                ok = true;
            }
        }
        else
        {
            printf ("rom too large [%zu bytes]\n", len);
        }

        fclose (fp);
    }

    printf ("loading rom [%s]: %s\n", file, (ok ? "OK" : "Failed"));

    return ok;
}

static void
handle_input (struct emulator *emu)
{
    SDL_Event event;
    while (SDL_PollEvent (&event) != 0)
    {
        switch (event.type)
        {
            case SDL_QUIT:
            {
                g__running = false;
            } break;
            case SDL_KEYDOWN:
            {
                SDL_Keycode code = event.key.keysym.sym;
                switch (code)
                {
                    case SDLK_ESCAPE:
                    {
                        g__running = false;
                    } break;
                    case SDLK_1: emu->keys[0x1] = 1; break;
                    case SDLK_2: emu->keys[0x2] = 1; break;
                    case SDLK_3: emu->keys[0x3] = 1; break;
                    case SDLK_4: emu->keys[0xC] = 1; break;
                    case SDLK_q: emu->keys[0x4] = 1; break;
                    case SDLK_w: emu->keys[0x5] = 1; break;
                    case SDLK_e: emu->keys[0x6] = 1; break;
                    case SDLK_r: emu->keys[0xD] = 1; break;
                    case SDLK_a: emu->keys[0x7] = 1; break;
                    case SDLK_s: emu->keys[0x8] = 1; break;
                    case SDLK_d: emu->keys[0x9] = 1; break;
                    case SDLK_f: emu->keys[0xE] = 1; break;
                    case SDLK_z: emu->keys[0xA] = 1; break;
                    case SDLK_x: emu->keys[0x0] = 1; break;
                    case SDLK_c: emu->keys[0xB] = 1; break;
                    case SDLK_v: emu->keys[0xF] = 1; break;
                }
            } break;
            case SDL_KEYUP:
            {
                SDL_Keycode code = event.key.keysym.sym;
                switch (code)
                {
                    case SDLK_1: emu->keys[0x1] = 0; break;
                    case SDLK_2: emu->keys[0x2] = 0; break;
                    case SDLK_3: emu->keys[0x3] = 0; break;
                    case SDLK_4: emu->keys[0xC] = 0; break;
                    case SDLK_q: emu->keys[0x4] = 0; break;
                    case SDLK_w: emu->keys[0x5] = 0; break;
                    case SDLK_e: emu->keys[0x6] = 0; break;
                    case SDLK_r: emu->keys[0xD] = 0; break;
                    case SDLK_a: emu->keys[0x7] = 0; break;
                    case SDLK_s: emu->keys[0x8] = 0; break;
                    case SDLK_d: emu->keys[0x9] = 0; break;
                    case SDLK_f: emu->keys[0xE] = 0; break;
                    case SDLK_z: emu->keys[0xA] = 0; break;
                    case SDLK_x: emu->keys[0x0] = 0; break;
                    case SDLK_c: emu->keys[0xB] = 0; break;
                    case SDLK_v: emu->keys[0xF] = 0; break;
                }
            } break;
        }
    }
}

static void 
emulation_cycle (struct emulator *emu)
{
    u16 opcode = emu->memory[emu->program_counter] << 8 | emu->memory[emu->program_counter + 1];

    switch (opcode & 0xF000)
    {
        case 0x0000:
        {
            switch (opcode & 0x00FF) // & 0x00FF
            {
                case 0x00E0: // 0x00E0
                {
                    // clear screen
                    for (u32 i = 0; i < 64 * 32; i++)
                    {
                        emu->gfx[i] = 0;
                    }
                    emu->draw_flag = true;
                    emu->program_counter += 2;
                } break;
                case 0x00EE:
                {
                    // return from subroutine

                    // pop the stored address off the stack
                    u16 address = emu->stack[--emu->stack_ptr];

                    // move to the stored address
                    emu->program_counter = address;
                    emu->program_counter += 2;
                } break;
                default:
                {
                    printf ("Unknown opcode [0x0000]: 0x%X\n", opcode);
                }
            }
        } break;
        case 0x1000:
        {
            // jump to address NNN
            emu->program_counter = opcode & 0x0FFF;
        } break;
        case 0x2000:
        {
            // call subroutine

            // store the program counter in the stack before we jump
            emu->stack[emu->stack_ptr++] = emu->program_counter;

            // jump to the address in the opcode
            emu->program_counter = opcode & 0x0FFF;
        } break;
        case 0x3000:
        {
            // 3XNN
            //
            // Skips the next instruction if VX equals NN.
            // (Usually the next instruction is a jump to skip a code block);
            u8 index = (opcode & 0x0F00) >> 8;
            u8 vx = emu->registers[index];
            u8 nn = opcode & 0x00FF;

            if (vx == nn)
            {
                emu->program_counter += 4;
            }
            else
            {
                emu->program_counter += 2;
            }
        } break;
        case 0x4000:
        {
            // 4XNN
            //
            // Skips the next instruction if VX does not equal NN.
            // (Usually the next instruction is a jump to skip a code block);
            u8 index = (opcode & 0x0F00) >> 8;
            u8 vx = emu->registers[index];
            u8 nn = opcode & 0x00FF;

            if (vx != nn)
            {
                emu->program_counter += 4;
            }
            else
            {
                emu->program_counter += 2;
            }
        } break;
        case 0x5000:
        {
            // 5XY0
            //
            // Skips the next instruction if VX equals VY.
            // (Usually the next instruction is a jump to skip a code block);
            u8 vx = emu->registers[(opcode & 0x0F00) >> 8];
            u8 vy = emu->registers[(opcode & 0x00F0) >> 4];

            if (vx == vy)
            {
                emu->program_counter += 4;
            }
            else
            {
                emu->program_counter += 2;
            }
        } break;
        case 0x6000:
        {
            // 6XNN
            //
            // set VX to NN
            emu->registers[(opcode & 0x0F00) >> 8] = opcode & 0x00FF;
            emu->program_counter += 2;
        } break;
        case 0x7000:
        {
            // 7XNN
            //
            // Adds NN to VX. (Carry flag is not changed);
            u8 index = (opcode & 0x0F00) >> 8;
            u8 nn = opcode & 0x00FF;
            emu->registers[index] += nn;
            emu->program_counter += 2;
        } break;
        case 0x8000:
        {
            switch (opcode & 0x000F)
            {
                case 0x0000:
                {
                    // 8XY0
                    //
                    // Set VX to the value of VY.
                    u8 vy = emu->registers[(opcode & 0x00F0) >> 4];
                    emu->registers[(opcode & 0x0F00) >> 8] = vy;
                    emu->program_counter += 2;
                } break;
                case 0x0001:
                {
                    // 8XY1
                    //
                    // Set VX to the value of VX | VY
                    u8 vy = emu->registers[(opcode & 0x00F0) >> 4];
                    emu->registers[(opcode & 0x0F00) >> 8] |= vy;
                    emu->program_counter += 2;
                } break;
                case 0x0002:
                {
                    // 8XY2
                    //
                    // Set VX to the value of VX & VY
                    u8 vy = emu->registers[(opcode & 0x00F0) >> 4];
                    emu->registers[(opcode & 0x0F00) >> 8] &= vy;
                    emu->program_counter += 2;
                } break;
                case 0x0003:
                {
                    // 8XY3
                    //
                    // Set VX to the value of VX ^ VY
                    u8 vy = emu->registers[(opcode & 0x00F0) >> 4];
                    emu->registers[(opcode & 0x0F00) >> 8] ^= vy;
                    emu->program_counter += 2;
                } break;
                case 0x0004:
                {
                    // 8XY4
                    //
                    // Add VY to VX. VF is set to 1 when there's a carry, 0 if not.
                    u8 vx = emu->registers[(opcode & 0x0F00) >> 8];
                    u8 vy = emu->registers[(opcode & 0x00F0) >> 4];

                    if (vy > vx)
                    {
                        emu->registers[0xF] = 1; // set carry register
                    }
                    else
                    {
                        emu->registers[0xF] = 0;
                    }
                    
                    emu->registers[(opcode & 0x0F00) >> 8] += vy;
                    emu->program_counter += 2;
                } break;
                case 0x0005:
                {
                    // 8XY5
                    //
                    // VY is subtracted from VX.
                    // VF is set to 0 when there's a borrow, and 1 when there is not.
                    u8 vx = emu->registers[(opcode & 0x0F00) >> 8];
                    u8 vy = emu->registers[(opcode & 0x00F0) >> 4];

                    if (vy > vx)
                    {
                        emu->registers[0xF] = 0;
                    }
                    else
                    {
                        emu->registers[0xF] = 1;
                    }
                    
                    emu->registers[(opcode & 0x0F00) >> 8] -= vy;
                    emu->program_counter += 2;
                } break;
                case 0x0006:
                {
                    // 8XY6
                    //
                    // Stores the least significant bit of VX in VF and then shifts VX to the right by 1.
                    u8 vx = emu->registers[(opcode & 0x0F00) >> 8];
                    emu->registers[0xF] = vx & 0x01;
                    emu->registers[(opcode & 0x0F00) >> 8] >>= 1;
                    emu->program_counter += 2;
                } break;
                case 0x0007:
                {
                    // 8XY7
                    //
                    // Sets VX to VY minus VX. VF is set to 0 when there's a borrow, and 1 when there is not.
                    u8 vx = emu->registers[(opcode & 0x0F00) >> 8];
                    u8 vy = emu->registers[(opcode & 0x00F0) >> 4];

                    if (vx > vy)
                    {
                        emu->registers[0xF] = 0; // borrow
                    }
                    else
                    {
                        emu->registers[0xF] = 1;
                    }

                    emu->registers[(opcode & 0x0F00) >> 8] = vy - vx;
                    emu->program_counter += 2;
                } break;
                case 0x000E:
                {
                    // 8XYE
                    //
                    // Stores the most significant bit of VX in VF and then shifts VX to the left by 1.
                    emu->registers[0xF] = emu->registers[(opcode & 0x0F00) >> 8] >> 7; 
                    emu->registers[(opcode & 0x0F00) >> 8] <<= 1;
                    emu->program_counter += 2;
                } break;
                default:
                {
                    printf ("Unknown opcode [0x8000]: 0x%X\n", opcode);
                }
            }
        } break;
        case 0x9000:
        {
            // 9XY0
            //
            // Skips the next instruction if VX does not equal VY.
            // (Usually the next instruction is a jump to skip a code block);
            u8 vx = emu->registers[VX (opcode)];
            u8 vy = emu->registers[(opcode & 0x00F0) >> 4];

            if (vx != vy)
            {
                emu->program_counter += 4;
            }
            else
            {
                emu->program_counter += 2;
            }
        } break;
        case 0xA000:
        {
            // ANNN
            //
            // Sets I to the address NNN.
            emu->index_register = opcode & 0x0FFF;
            emu->program_counter += 2;
        } break;
        case 0xB000:
        {
            // BNNN
            //
            // Jumps to the address NNN plus V0.
            emu->program_counter = (opcode & 0x0FFF) + emu->registers[0];
        } break;
        case 0xC000:
        {
            // CXNN
            //
            // Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN.
            emu->registers[(opcode & 0x0F00) >> 8] = (rand () % 0xFF) & (opcode & 0x00FF);
            emu->program_counter += 2;
        } break;
        case 0xD000:
        {
            // DXYN
            //
            // Draws a sprite at coordinate (VX, VY) that has a width of 8 pixels
            // and a height of N pixels. Each row of 8 pixels is read as bit-coded
            // starting from memory location I; I value does not change after the
            // execution of this instruction. As described above, VF is set to 1 if
            // any screen pixels are flipped from set to unset when the sprite is
            // drawn, and to 0 if that does not happen

            u8 vx = emu->registers[(opcode & 0x0F00) >> 8];
            u8 vy = emu->registers[(opcode & 0x00F0) >> 4];
            u32 width = 8;
            u32 height = opcode & 0x000F;

            // wrap the starting coordinates
            u8 x = vx % 64; // or vx & 0x3F
            u8 y = vy % 32; // or vy & 0x1F

            // vx = 50
            // vy = 30
            // w = 8
            // h = 8
            // I = 0x200
            // y = 0
            // row = 1011 0011 (value @ 0x200)
            // x = 0
            // col = 1000 0000
            // dest_pixel = ((30 + 0) * 64) + (50 + 0)

            emu->registers[0xF] = 0;
            for (u32 y = 0; y < height; y++)
            {
                u8 row = emu->memory[emu->index_register + y];
                for (u32 x = 0; x < width; x++)
                {
                    u8 col = row & (0x80 >> x);
                    if (col != 0)
                    {
                        // clamp
                        u32 dest_x = (vx + x > 64 ? 64 : vx + x);
                        u32 dest_y = (vy + y > 32 ? 32 : vy + y);
                        u32 dest_pixel = (dest_y * 64) + dest_x;
                        if (emu->gfx[dest_pixel] == 0xFFFFFFFF)
                        {
                            emu->registers[0xF] = 1; // collision
                        }

                        emu->gfx[dest_pixel] ^= 0xFFFFFFFF;
                    }
                }
            }

            emu->draw_flag = true;
            emu->program_counter += 2;
        } break;
        case 0xE000:
        {
            switch (opcode & 0x00FF)
            {
                case 0x009E:
                {
                    // EX9E
                    //
                    // Skips the next instruction if the key stored in VX is pressed.
                    // (Usually the next instruction is a jump to skip a code block);
                    u8 vx = emu->registers[VX (opcode)];
                    if (emu->keys[vx] != 0)
                    {
                        emu->program_counter += 4;
                    }
                    else
                    {
                        emu->program_counter += 2;
                    }
                } break;
                case 0x00A1:
                {
                    // EXA1
                    //
                    // Skips the next instruction if the key stored in VX is not pressed.
                    // (Usually the next instruction is a jump to skip a code block);
                    u8 vx = emu->registers[VX (opcode)];
                    if (emu->keys[vx] == 0)
                    {
                        emu->program_counter += 4;
                    }
                    else
                    {
                        emu->program_counter += 2;
                    }
                } break;
                default:
                {
                    printf ("Unknown opcode [0xE000]: 0x%X\n", opcode);
                }
            }
        } break;
        case 0xF000:
        {
            switch (opcode & 0x00FF)
            {
                case 0x0007:
                {
                    // FX07
                    //
                    // Sets VX to the value of the delay timer.
                    emu->registers[VX (opcode)] = emu->delay_timer;
                    emu->program_counter += 2;
                } break;
                case 0x000A:
                {
                    // FX0A
                    //
                    // A key press is awaited, and then stored in VX.
                    // (Blocking Operation. All instruction halted until next key event);
                    bool key_pressed = false;

                    for (u32 i = 0; i < sizeof (emu->keys); i++)
                    {
                        if (emu->keys[i] != 0)
                        {
                            emu->registers[VX (opcode)] = i;
                            key_pressed = true;
                            //break;
                        }
                    }

                    if (key_pressed)
                    {
                        emu->program_counter += 2; 
                    }
                    else
                    {
                        return;
                    }
                } break;
                case 0x0015:
                {
                    // FX15
                    //
                    // Sets the delay timer to VX.
                    u8 vx = emu->registers[VX (opcode)];
                    emu->delay_timer = vx;
                    emu->program_counter += 2;
                } break;
                case 0x0018:
                {
                    // FX18
                    //
                    // Sets the sound timer to VX.
                    u8 vx = emu->registers[VX (opcode)];
                    emu->sound_timer = vx;
                    emu->program_counter += 2;
                } break;
                case 0x001E:
                {
                    // FX1E
                    //
                    // Adds VX to I. VF is not affected
                    u8 vx = emu->registers[VX (opcode)];
                    if (emu->index_register + vx > 0xFFF)
                    {
                        emu->registers[0xF] = 1;
                    }
                    else
                    {
                        emu->registers[0xF] = 0;
                    }
                    emu->index_register += vx;
                    emu->program_counter += 2;
                } break;
                case 0x0029:
                {
                    // FX29
                    //
                    // Sets I to the location of the sprite for the character in VX.
                    // Characters 0-F (in hexadecimal) are represented by a 4x5 font.
                    u8 vx = emu->registers[VX (opcode)];
                    // emu->index_register = vx * 0x50;
                    emu->index_register = 0x50 + (vx * 5);
                    emu->program_counter += 2;
                } break;
                case 0x0033:
                {
                    // FX33
                    //
                    // Stores the binary-coded decimal representation of VX, with
                    // the most significant of three digits at the address in I,
                    // the middle digit at I plus 1, and the least significant digit
                    // at I plus 2. (In other words, take the decimal representation
                    // of VX, place the hundreds digit in memory at location in I,
                    // the tens digit at location I+1, and the ones digit at location I+2.);
                    u16 I = emu->index_register;
                    u8 vx = emu->registers[VX (opcode)];

                    emu->memory[I]     = vx / 100;
                    emu->memory[I + 1] = (vx / 10) % 10;
                    emu->memory[I + 2] = (vx % 100) % 10;

                    emu->program_counter += 2;
                } break;
                case 0x0055:
                {
                    // FX55
                    //
                    // Stores from V0 to VX (including VX) in memory, starting at address I.
                    // The offset from I is increased by 1 for each value written, but I itself is left unmodified.
                    u8 vx = VX (opcode);
                    for (u32 i = 0; i <= vx; i++)
                    {
                        emu->memory[emu->index_register + i] = emu->registers[i];
                    }

                    // The original chip-8 interpreter left I incremented once the operation finished.
                    // emu->index_register += vx + 1;
                    emu->program_counter += 2;
                } break;
                case 0x0065:
                {
                    // FX65
                    //
                    // Fills from V0 to VX (including VX) with values from memory, starting at address I.
                    // The offset from I is increased by 1 for each value written, but I itself is left unmodified.
                    u8 vx = VX (opcode);

                    for (u32 i = 0; i <= vx; i++)
                    {
                        emu->registers[i] = emu->memory[emu->index_register + i];
                    }

                    // The original chip-8 interpreter left I incremented once the operation finished.
                    // emu->index_register += vx + 1;
                    emu->program_counter += 2;
                } break;
                default:
                {
                    printf ("Unknown opcode [0xF000]: 0x%X\n", opcode);
                }
            } 
        } break;
        default:
        {
            printf ("Unknown opcode: 0x%X\n", opcode);
        }
    }

    if (emu->delay_timer > 0)
    {
        --emu->delay_timer;
    }

    if (emu->sound_timer > 0)
    {
        if (emu->sound_timer == 1)
        {
            printf ("BEEP!\n");
        }
        --emu->sound_timer;
    }
}

int
main (int argc, char **argv)
{
    char *rom = "roms/tetris.c8";
    struct emulator emu = {0};
    int delay = 3;

    init (&emu, 64 * 10, 32 * 10, 64, 32);

    int pitch = sizeof (emu.gfx[0]) * 64;

    if (argc == 2)
    {
        rom = argv[1];
    }

    char *filename = strrchr (rom, '\\');
    if (filename)
    {
        filename++;
    }
    else
    {
        filename = rom;
    }

    if (strcmp (filename, "invaders.c8") == 0)
    {
        delay = 1;
    }
    else if (strcmp (filename, "tetris.c8") == 0)
    {
        delay = 3;
    }

    printf ("rom: %s\n", filename);

    if (!load_rom (&emu, rom))
    {
        return 1;
    }

    while (g__running)
    {
        handle_input (&emu); 

        emulation_cycle (&emu);

        SDL_UpdateTexture (emu.texture, NULL, emu.gfx, pitch);
        SDL_RenderClear (emu.renderer);
        SDL_RenderCopy (emu.renderer, emu.texture, NULL, NULL);
        SDL_RenderPresent (emu.renderer);

        SDL_Delay (delay);
    }

    deinit (&emu);

    return 0;
}
