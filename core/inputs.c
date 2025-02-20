// -------------------------------------------------------------
//  Cubzh Core
//  inputs.c
//  Created by Adrien Duermael on February 24, 2020.
// -------------------------------------------------------------

#include "inputs.h"

#include "doubly_linked_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cclog.h"
#include "config.h"

#define MOVE_EVENTS_TO_SKIP 2

typedef struct {
    float x;
    float y;
    bool leftButtonDown;
    bool middleButtonDown;
    bool rightButtonDown;
    char pad;
} Mouse;

typedef struct {
    float x;
    float y;
    TouchState state;
    bool down;
    uint8_t skippedMoves;
    char pad[2];
} Touch;

// represents the context of inputs (mouse, touch, & keyboard events)
struct _InputContext {
    // current situation regarding touches
    Touch touches[2]; // supporting 2 touches maximum, increase to support more

    // current situation for the mouse
    Mouse mouse;

    uint8_t modifiers;

    uint8_t nbPressedInputs; // max = 10

    char pad[2];

    Input pressedInputs[10]; // more than 10 pressed inputs is not supported...

    FifoList *touchEventPool;     // pool of recycled touch events
    FifoList *mouseEventPool;     // pool of recycled mouse events
    FifoList *keyEventPool;       // pool of recycled key events
    FifoList *charEventPool;      // pool of recycled char events
    FifoList *dirPadEventPool;    // pool of recycled dir pad events
    FifoList *actionPadEventPool; // pool of recycled action pad events
    FifoList *analogPadEventPool; // pool of recycled analog pad events

    // Input event listeners
    DoublyLinkedList *listeners;

    InputEventWithHistory pressedInputsImgui[10]; // more than 10 pressed inputs is not supported...
    uint8_t nbPressedInputsImgui;                 // max = 10

    // All events are posted in pixel coords, but we do use points then everywhere
    // Knowing the scale factor allows us to do the conversion when events are posted.
    float nbPixelsInOnePoint;

    // true by default, but can be set to false to refuse all inputs
    bool acceptInputs;
};

struct _InputListener {

    // last popped MouseEvent
    MouseEvent *poppedMouseEvent;

    // last popped TouchEvent
    TouchEvent *poppedTouchEvent;

    // last popped KeyEvent
    KeyEvent *poppedKeyEvent;

    // last popped CharEvent
    CharEvent *poppedCharEvent;

    // last popped DirPadEvent
    DirPadEvent *poppedDirPadEvent;

    // last popped ActionPadEvent
    ActionPadEvent *poppedActionPadEvent;

    // last popped ActionPadEvent
    AnalogPadEvent *poppedAnalogPadEvent;

    FifoList *mouseEvents;
    FifoList *touchEvents;
    FifoList *keyEvents;
    FifoList *charEvents;
    FifoList *dirPadEvents;
    FifoList *actionPadEvents;
    FifoList *analogPadEvents;

    bool acceptsMouseEvents;
    bool acceptsTouchEvents;
    bool acceptsKeyEvents;
    bool acceptsCharEvents;
    bool acceptsDirPadEvents;
    bool acceptsActionPadEvents;
    bool acceptsAnalogPadEvents;

    bool acceptsRepeatedKeyDown;
    bool acceptsRepeatedChar;
};

InputContext *inputContext(void) {

    static InputContext *c = NULL;

    if (c == NULL) {

        c = (InputContext *)malloc(sizeof(InputContext));

        c->touches[0].x = 0.0f;
        c->touches[0].y = 0.0f;
        c->touches[0].state = TouchStateNone;
        c->touches[0].down = false;
        c->touches[0].skippedMoves = 0;

        c->touches[1].x = 0.0f;
        c->touches[1].y = 0.0f;
        c->touches[1].state = TouchStateNone;
        c->touches[1].down = false;
        c->touches[1].skippedMoves = 0;

        c->mouse.x = 0.0f;
        c->mouse.y = 0.0f;
        c->mouse.leftButtonDown = false;
        c->mouse.middleButtonDown = false;
        c->mouse.rightButtonDown = false;

        c->modifiers = ModifierNone;

        c->nbPressedInputs = 0;

        c->touchEventPool = fifo_list_new();
        c->mouseEventPool = fifo_list_new();
        c->keyEventPool = fifo_list_new();
        c->charEventPool = fifo_list_new();
        c->dirPadEventPool = fifo_list_new();
        c->actionPadEventPool = fifo_list_new();
        c->analogPadEventPool = fifo_list_new();

        c->listeners = doubly_linked_list_new();

        c->nbPressedInputsImgui = 0;

        c->nbPixelsInOnePoint = 1.0f;

        c->acceptInputs = true;
    }

    return c;
}

void inputs_accept(const bool b) {
    inputContext()->acceptInputs = b;
}

void inputs_set_nb_pixels_in_one_point(const float f) {
    inputContext()->nbPixelsInOnePoint = f;
}

MouseEvent *recycle_mouse_event(void) {
    MouseEvent *me = (MouseEvent *)fifo_list_pop(inputContext()->mouseEventPool);
    if (me == NULL) { // if no event available, create one
        me = (MouseEvent *)malloc(sizeof(MouseEvent));
        if (me == NULL) {
            cclog_error("🔥 can't alloc MouseEvent");
        } else {
            me->eventType = mouseEvent;
        }
    }
    return me;
}

TouchEvent *recycle_touch_event(void) {
    TouchEvent *te = (TouchEvent *)fifo_list_pop(inputContext()->touchEventPool);
    if (te == NULL) { // if no event available, create one
        te = (TouchEvent *)malloc(sizeof(TouchEvent));
        if (te == NULL) {
            cclog_error("🔥 can't alloc TouchEvent");
        } else {
            te->eventType = touchEvent;
        }
    }
    return te;
}

KeyEvent *recycle_key_event(void) {
    KeyEvent *ke = (KeyEvent *)fifo_list_pop(inputContext()->keyEventPool);
    if (ke == NULL) { // if no event available, create one
        ke = (KeyEvent *)malloc(sizeof(KeyEvent));
        if (ke == NULL) {
            cclog_error("🔥 can't alloc KeyEvent");
        } else {
            ke->eventType = keyEvent;
        }
    }
    return ke;
}

CharEvent *recycle_char_event(void) {
    CharEvent *ce = (CharEvent *)fifo_list_pop(inputContext()->charEventPool);
    if (ce == NULL) { // if no event available, create one
        ce = (CharEvent *)malloc(sizeof(CharEvent));
        if (ce == NULL) {
            cclog_error("🔥 can't alloc CharEvent");
        } else {
            ce->eventType = charEvent;
        }
    }
    return ce;
}

DirPadEvent *recycle_dirpad_event(void) {
    DirPadEvent *de = (DirPadEvent *)fifo_list_pop(inputContext()->dirPadEventPool);
    if (de == NULL) { // if no event available, create one
        de = (DirPadEvent *)malloc(sizeof(DirPadEvent));
        if (de == NULL) {
            cclog_error("🔥 can't alloc DirPadEvent");
        } else {
            de->eventType = dirPadEvent;
        }
    }
    return de;
}

ActionPadEvent *recycle_actionpad_event(void) {
    ActionPadEvent *ae = (ActionPadEvent *)fifo_list_pop(inputContext()->actionPadEventPool);
    if (ae == NULL) { // if no event available, create one
        ae = (ActionPadEvent *)malloc(sizeof(ActionPadEvent));
        if (ae == NULL) {
            cclog_error("🔥 can't alloc ActionPadEvent");
        } else {
            ae->eventType = actionPadEvent;
        }
    }
    return ae;
}

AnalogPadEvent *recycle_analogpad_event(void) {
    AnalogPadEvent *ce = (AnalogPadEvent *)fifo_list_pop(inputContext()->analogPadEventPool);

    if (ce == NULL) { // if no event available, create one
        ce = (AnalogPadEvent *)malloc(sizeof(AnalogPadEvent));
        if (ce == NULL) {
            cclog_error("🔥 can't alloc AnalogPadEvent");
        } else {
            ce->eventType = analogPadEvent;
        }
    }
    return ce;
}

void input_get_cursor(float *x, float *y, bool *btn1, bool *btn2, bool *btn3) {
    if (x != NULL) {
        *x = inputContext()->mouse.x;
    }
    if (y != NULL) {
        *y = inputContext()->mouse.y;
    }
    if (btn1 != NULL) {
        *btn1 = inputContext()->mouse.leftButtonDown;
    }
    if (btn2 != NULL) {
        *btn2 = inputContext()->mouse.rightButtonDown;
    }
    if (btn3 != NULL) {
        *btn3 = inputContext()->mouse.middleButtonDown;
    }
}

bool input_shiftIsOn(void) {
    return inputContext()->modifiers & ModifierShift;
}

bool input_altIsOn(void) {
    return inputContext()->modifiers & ModifierAlt;
}

bool input_ctrlIsOn(void) {
    return inputContext()->modifiers & ModifierCtrl;
}

bool input_superIsOn(void) {
    return inputContext()->modifiers & ModifierSuper;
}

bool input_isOn(Input input) {
    InputContext *c = inputContext();
    for (uint8_t i = 0; i < c->nbPressedInputs; i++) {
        if (c->pressedInputs[i] == input) {
            return true;
        }
    }
    return false;
}

int input_nb_pressed_inputs(void) {
    return (int)inputContext()->nbPressedInputs;
}

const Input *input_pressed_inputs(void) {
    return inputContext()->pressedInputs;
}

uint8_t *input_nbPressedInputsImGui(void) {
    return &(inputContext()->nbPressedInputsImgui);
}

InputEventWithHistory *input_pressedInputsImGui(void) {
    return inputContext()->pressedInputsImgui;
}

void postMouseEvent(float x,
                    float y,
                    float dx,
                    float dy,
                    MouseButton button,
                    bool down,
                    bool move) {

    InputContext *c = inputContext();

    if (c->acceptInputs == false)
        return;

    MouseEvent *me = recycle_mouse_event();
    if (me == NULL) {
        return;
    }

    if (button != MouseButtonScroll) {
        // update context
        c->mouse.x = x / inputContext()->nbPixelsInOnePoint;
        c->mouse.y = y / inputContext()->nbPixelsInOnePoint;
        switch (button) {
            case MouseButtonLeft:
                c->mouse.leftButtonDown = down;
                break;
            case MouseButtonRight:
                c->mouse.rightButtonDown = down;
                break;
            case MouseButtonMiddle:
                c->mouse.middleButtonDown = down;
                break;
            default:
                break;
        }
    }

    me->x = x / inputContext()->nbPixelsInOnePoint;
    me->y = y / inputContext()->nbPixelsInOnePoint;
    me->dx = dx / inputContext()->nbPixelsInOnePoint;
    me->dy = dy / inputContext()->nbPixelsInOnePoint;
    me->button = button;
    me->down = down;
    me->move = move;

    DoublyLinkedListNode *node = doubly_linked_list_last(c->listeners);
    while (node != NULL) {

        InputListener *il = (InputListener *)doubly_linked_list_node_pointer(node);

        if (il->acceptsMouseEvents) {
            MouseEvent *me2 = recycle_mouse_event();
            if (me2 == NULL) {
                break; // exit the while loop
            }

            mouse_event_copy(me, me2);

            fifo_list_push(il->mouseEvents, me2);
        }

        node = doubly_linked_list_node_previous(node);
    }

    // put `me` mouse event back into the recycle pool
    fifo_list_push(c->mouseEventPool, me);
    me = NULL;
}

void postTouchEvent(uint8_t ID, float x, float y, float dx, float dy, TouchState state, bool move) {

    InputContext *c = inputContext();

    if (c->acceptInputs == false)
        return;
    if (ID >= TOUCH_EVENT_MAXCOUNT)
        return;

    // First move deltas can be big on touch screens because the
    // system ignores small movements at first.
    // Skipping first move events to avoid undesired jumps.
    if (state == TouchStateNone) { // move event
        if (c->touches[ID].skippedMoves < MOVE_EVENTS_TO_SKIP) {
            c->touches[ID].skippedMoves += 1;
            return;
        }
    }

    TouchEvent *te = recycle_touch_event();
    if (te == NULL) {
        return;
    }

    te->ID = ID;
    te->x = x / inputContext()->nbPixelsInOnePoint;
    te->y = y / inputContext()->nbPixelsInOnePoint;
    te->dx = dx / inputContext()->nbPixelsInOnePoint;
    te->dy = dy / inputContext()->nbPixelsInOnePoint;
    te->state = state;
    te->move = move;

    // NOTE: aduermael: This is temporary
    // We consider first finger touch events to be mouse events
    // so that imgui can work with our current "mouse only" implementation.
    // We should get rid of this at some point.
    if (ID == 0) {
        switch (state) {
            case TouchStateDown:
                c->mouse.x = te->x;
                c->mouse.y = te->y;
                c->mouse.leftButtonDown = true;
                break;
            case TouchStateUp:
                c->mouse.x = te->x;
                c->mouse.y = te->y;
                c->mouse.leftButtonDown = false;
                break;
            case TouchStateCanceled:
                c->mouse.leftButtonDown = false;
            case TouchStateNone: // move
                c->mouse.x = te->x;
                c->mouse.y = te->y;
                break;
        }
    }

    // Store touch state
    if (ID == 0 || ID == 1) {

        Touch *t = &(c->touches[ID]);

        // cclog_debug("ID: %d, x: %.2f y: %.2f", ID, te->x, te->y);

        switch (state) {
            case TouchStateDown:
                t->x = te->x;
                t->y = te->y;
                t->state = TouchStateDown;
                t->down = true;
                c->touches[ID].skippedMoves = 0;
                break;
            case TouchStateUp:
                t->x = te->x;
                t->y = te->y;
                t->state = TouchStateUp;
                t->down = false;
                break;
            case TouchStateCanceled:
                t->state = TouchStateCanceled;
                t->down = false;
                break;
            case TouchStateNone:
                t->x = te->x;
                t->y = te->y;
                break;
        }

        // cclog_debug("💾 t%d: %.2f,%.2f", ID, inputContext()->touches[ID].x,
        // inputContext()->touches[ID].y);
    }

    DoublyLinkedListNode *node = doubly_linked_list_last(c->listeners);
    while (node != NULL) {

        InputListener *il = (InputListener *)doubly_linked_list_node_pointer(node);

        if (il->acceptsTouchEvents) {

            TouchEvent *te2 = recycle_touch_event();
            if (te2 == NULL) {
                break; // exit the while loop
            }

            touch_event_copy(te, te2);

            fifo_list_push(il->touchEvents, te2);
        }

        node = doubly_linked_list_node_previous(node);
    }

    // put `te` touch event back into the recycle pool
    fifo_list_push(c->touchEventPool, te);
    te = NULL;
}

void postKeyEvent(Input input, uint8_t modifiers, KeyState state) {

    InputContext *c = inputContext();

    if (c->acceptInputs == false)
        return;

    KeyEvent *ke = recycle_key_event();
    if (ke == NULL) {
        return;
    }

    ke->input = input;
    ke->modifiers = modifiers;
    ke->state = state;

    // update context

    c->modifiers = ke->modifiers;

    // some listeners do not accept repeated key down events
    bool repeated = false;

    if (ke->state == KeyStateDown) { // add to array
        bool insert = true;

        if (c->nbPressedInputs >= 10) {
            insert = false;
        } else {
            // make sure input is not already registered
            for (uint8_t i = 0; i < c->nbPressedInputs; i++) {
                if (c->pressedInputs[i] == ke->input) {
                    // should not happen, but input already found, skip
                    insert = false;
                    repeated = true;
                    break;
                }
            }
        }

        if (insert) {
            c->pressedInputs[c->nbPressedInputs] = ke->input;
            c->nbPressedInputs++;
        }

    } else if (ke->state == KeyStateUp) { // remove from array
        int indexToRemove = -1;
        // find index to remove
        for (uint8_t i = 0; i < c->nbPressedInputs; i++) {
            if (c->pressedInputs[i] == ke->input) {
                indexToRemove = (int)i;
                break;
            }
        }
        if (indexToRemove >= 0) {
            // replace by last member if input was not last
            if (indexToRemove < c->nbPressedInputs - 1) {
                c->pressedInputs[indexToRemove] = c->pressedInputs[c->nbPressedInputs - 1];
            }
            c->nbPressedInputs--;
        }
    }

    //
    // process event for ImGui
    //
    if (ke->state == KeyStateDown) { // add to array
        if (c->nbPressedInputsImgui < 10) {
            bool insert = true;
            // make sure input is not already registered
            for (uint8_t i = 0; i < c->nbPressedInputsImgui; i++) {
                if (c->pressedInputsImgui[i].input == ke->input) {
                    c->pressedInputsImgui[i].stateFirst = KeyStateDown;
                    c->pressedInputsImgui[i].stateSecond = KeyStateUnknown;
                    c->pressedInputsImgui[i].seenByImGui = false;
                    insert = false;
                    break;
                }
            }
            if (insert) {
                c->pressedInputsImgui[c->nbPressedInputsImgui].input = ke->input;
                c->pressedInputsImgui[c->nbPressedInputsImgui].stateFirst = KeyStateDown;
                c->pressedInputsImgui[c->nbPressedInputsImgui].stateSecond = KeyStateUnknown;
                c->pressedInputsImgui[c->nbPressedInputsImgui].seenByImGui = false;
                c->nbPressedInputsImgui++;
            }
        } // else, ignore the 11th event
    } else if (ke->state == KeyStateUp) {
        int index = -1;
        for (uint8_t i = 0; i < c->nbPressedInputsImgui; i++) {
            if (c->pressedInputsImgui[i].input == ke->input) {
                index = i;
                break;
            }
        }
        if (index > -1) {
            if (c->pressedInputsImgui[index].seenByImGui == true) {
                // remove from array
                c->pressedInputsImgui[index] = c->pressedInputsImgui[c->nbPressedInputsImgui - 1];
                c->nbPressedInputsImgui--;
            } else {
                if (c->pressedInputsImgui[index].stateFirst == KeyStateDown) {
                    c->pressedInputsImgui[index].stateSecond = KeyStateUp;
                } else {
                    cclog_warning("this should not be");
                }
            }
        } else {
            cclog_error("input not found (for down)");
        }
    }

    DoublyLinkedListNode *node = doubly_linked_list_last(c->listeners);
    while (node != NULL) {

        void *ptr = (InputListener *)doubly_linked_list_node_pointer(node);

        InputListener *il = (InputListener *)ptr;

        if (il->acceptsKeyEvents) {

            if (il->acceptsRepeatedKeyDown == false && repeated) {
                node = doubly_linked_list_node_previous(node);
                continue;
            }

            KeyEvent *ke2 = recycle_key_event();
            if (ke2 == NULL) {
                break;
            }

            key_event_copy(ke, ke2);

            fifo_list_push(il->keyEvents, ke2);
        }

        node = doubly_linked_list_node_previous(node);
    }

    // put `ke` key event back into the recycle pool
    fifo_list_push(c->keyEventPool, ke);
    ke = NULL;
}

void postCharEvent(unsigned int inputChar) {

    InputContext *c = inputContext();

    if (c->acceptInputs == false)
        return;

    CharEvent *ce = recycle_char_event();
    if (ce == NULL) {
        return;
    }

    ce->inputChar = inputChar;

    DoublyLinkedListNode *node = doubly_linked_list_last(c->listeners);
    while (node != NULL) {

        InputListener *il = (InputListener *)doubly_linked_list_node_pointer(node);

        if (il->acceptsCharEvents) {

            CharEvent *ce2 = recycle_char_event();
            if (ce2 == NULL) {
                break;
            }

            char_event_copy(ce, ce2);

            fifo_list_push(il->charEvents, ce2);
        }

        node = doubly_linked_list_node_previous(node);
    }

    // put `ce` char event back into the recycle pool
    fifo_list_push(c->charEventPool, ce);
    ce = NULL;
}

void postDirPadEvent(float dx, float dy, PadBtnState state) {

    InputContext *c = inputContext();

    if (c->acceptInputs == false)
        return;

    DirPadEvent *de = recycle_dirpad_event();
    if (de == NULL) {
        return;
    }

    de->state = state;
    de->dx = dx;
    de->dy = dy;

    DoublyLinkedListNode *node = doubly_linked_list_last(c->listeners);
    while (node != NULL) {

        InputListener *il = (InputListener *)doubly_linked_list_node_pointer(node);

        if (il->acceptsDirPadEvents) {

            DirPadEvent *de2 = recycle_dirpad_event();
            if (de2 == NULL) {
                break; // exit the while loop
            }

            dir_pad_event_copy(de, de2);

            fifo_list_push(il->dirPadEvents, de2);
        }

        node = doubly_linked_list_node_previous(node);
    }

    // put `de` dirpad event back into the recycle pool
    fifo_list_push(c->dirPadEventPool, de);
    de = NULL;
}

void postActionPadEvent(ActionPadBtn button, PadBtnState state) {

    InputContext *c = inputContext();

    if (c->acceptInputs == false)
        return;

    ActionPadEvent *ae = recycle_actionpad_event();
    if (ae == NULL) {
        return;
    }

    ae->state = state;
    ae->button = button;

    DoublyLinkedListNode *node = doubly_linked_list_last(c->listeners);
    while (node != NULL) {

        InputListener *il = (InputListener *)doubly_linked_list_node_pointer(node);

        if (il->acceptsActionPadEvents) {

            ActionPadEvent *ae2 = recycle_actionpad_event();
            if (ae2 == NULL) {
                break;
            }

            action_pad_event_copy(ae, ae2);

            fifo_list_push(il->actionPadEvents, ae2);
        }

        node = doubly_linked_list_node_previous(node);
    }

    // put `ae` actionPad event back into the recycle pool
    fifo_list_push(c->actionPadEventPool, ae);
    ae = NULL;
}

void postAnalogPadEvent(float dx, float dy, PadBtnState state) {

    InputContext *c = inputContext();

    if (c->acceptInputs == false)
        return;

    AnalogPadEvent *ce = recycle_analogpad_event();
    if (ce == NULL) {
        return;
    }

    ce->state = state;
    ce->dx = dx;
    ce->dy = dy;

    DoublyLinkedListNode *node = doubly_linked_list_last(c->listeners);
    while (node != NULL) {

        InputListener *il = (InputListener *)doubly_linked_list_node_pointer(node);

        if (il->acceptsAnalogPadEvents) {

            AnalogPadEvent *ce2 = recycle_analogpad_event();
            if (ce2 == NULL) {
                break;
            }

            analog_pad_event_copy(ce, ce2);
            fifo_list_push(il->analogPadEvents, ce2);
        }

        node = doubly_linked_list_node_previous(node);
    }

    // put `ce` analogPad event back into the recycle pool
    fifo_list_push(c->analogPadEventPool, ce);
    ce = NULL;
}

// casts event to MouseEvent, returns NULL if event is not a MouseEvent
MouseEvent *input_event_to_MouseEvent(void *e) {
    if (e == NULL) {
        return NULL;
    }
    if (*(EventType *)e == mouseEvent) {
        return (MouseEvent *)e;
    }
    return NULL;
}

// casts event to TouchEvent, returns NULL if event is not a TouchEvent
TouchEvent *input_event_to_TouchEvent(void *e) {
    if (e == NULL) {
        return NULL;
    }
    if (*(EventType *)e == touchEvent) {
        return (TouchEvent *)e;
    }
    return NULL;
}

// casts event to KeyEvent, returns NULL if event is not a KeyEvent
KeyEvent *input_event_to_KeyEvent(void *e) {
    if (e == NULL) {
        return NULL;
    }
    if (*(EventType *)e == keyEvent) {
        return (KeyEvent *)e;
    }
    return NULL;
}

// casts event to CharEvent, returns NULL if event is not a CharEvent
CharEvent *input_event_to_CharEvent(void *e) {
    if (e == NULL) {
        return NULL;
    }
    if (*(EventType *)e == charEvent) {
        return (CharEvent *)e;
    }
    return NULL;
}

// casts event to DirPadEvent, returns NULL if event is not a DirPadEvent
DirPadEvent *input_event_to_DirPadEvent(void *e) {
    if (e == NULL) {
        return NULL;
    }
    if (*(EventType *)e == dirPadEvent) {
        return (DirPadEvent *)e;
    }
    return NULL;
}

// casts event to ActionPadEvent, returns NULL if event is not a ActionPadEvent
ActionPadEvent *input_event_to_ActionPadEvent(void *e) {
    if (e == NULL) {
        return NULL;
    }
    if (*(EventType *)e == actionPadEvent) {
        return (ActionPadEvent *)e;
    }
    return NULL;
}

// casts event to AnalogPadEvent, returns NULL if event is not a AnalogPadEvent
AnalogPadEvent *input_event_to_AnalogPadEvent(void *e) {
    if (e == NULL) {
        return NULL;
    }
    if (*(EventType *)e == analogPadEvent) {
        return (AnalogPadEvent *)e;
    }
    return NULL;
}

// ----------------------
// InputListener
// ----------------------

InputListener *input_listener_new(bool mouseEvents,
                                  bool touchEvents,
                                  bool keyEvents,
                                  bool charEvents,
                                  bool dirPadEvents,
                                  bool actionPadEvents,
                                  bool analogPadEvents,
                                  bool acceptsRepeatedKeyDown,
                                  bool acceptsRepeatedChar) {

    InputListener *il = (InputListener *)malloc(sizeof(InputListener));

    il->mouseEvents = fifo_list_new();
    il->touchEvents = fifo_list_new();
    il->keyEvents = fifo_list_new();
    il->charEvents = fifo_list_new();
    il->dirPadEvents = fifo_list_new();
    il->actionPadEvents = fifo_list_new();
    il->analogPadEvents = fifo_list_new();

    il->poppedMouseEvent = NULL;
    il->poppedTouchEvent = NULL;
    il->poppedKeyEvent = NULL;
    il->poppedCharEvent = NULL;
    il->poppedDirPadEvent = NULL;
    il->poppedActionPadEvent = NULL;
    il->poppedAnalogPadEvent = NULL;

    il->acceptsMouseEvents = mouseEvents;
    il->acceptsTouchEvents = touchEvents;
    il->acceptsKeyEvents = keyEvents;
    il->acceptsCharEvents = charEvents;
    il->acceptsDirPadEvents = dirPadEvents;
    il->acceptsActionPadEvents = actionPadEvents;
    il->acceptsAnalogPadEvents = analogPadEvents;

    il->acceptsRepeatedKeyDown = acceptsRepeatedKeyDown;
    il->acceptsRepeatedChar = acceptsRepeatedChar;

    if (il != NULL) {
        doubly_linked_list_push_first(inputContext()->listeners, (void *)il);
    }

    return il;
}

void input_listener_free(InputListener *il) {

    InputContext *c = inputContext();

    // remove from input context listeners
    DoublyLinkedList *listeners = c->listeners;
    DoublyLinkedListNode *node = doubly_linked_list_last(listeners);
    while (node != NULL) {
        void *ptr = doubly_linked_list_node_pointer(node);

        if (il == ptr) {
            doubly_linked_list_delete_node(listeners, node);
            break;
        }
        node = doubly_linked_list_node_previous(node);
    }

    // pop to recycle events that haven't been consumed
    while (input_listener_pop_mouse_event(il) != NULL)
        ;
    while (input_listener_pop_touch_event(il) != NULL)
        ;
    while (input_listener_pop_key_event(il) != NULL)
        ;
    while (input_listener_pop_char_event(il) != NULL)
        ;
    while (input_listener_pop_dir_pad_event(il) != NULL)
        ;
    while (input_listener_pop_action_pad_event(il) != NULL)
        ;
    while (input_listener_pop_analog_pad_event(il) != NULL)
        ;

    fifo_list_free(il->mouseEvents, NULL);
    fifo_list_free(il->touchEvents, NULL);
    fifo_list_free(il->keyEvents, NULL);
    fifo_list_free(il->charEvents, NULL);
    fifo_list_free(il->dirPadEvents, NULL);
    fifo_list_free(il->actionPadEvents, NULL);
    fifo_list_free(il->analogPadEvents, NULL);

    if (il->poppedMouseEvent != NULL) {
        fifo_list_push(c->mouseEventPool, il->poppedMouseEvent);
        il->poppedMouseEvent = NULL;
    }

    if (il->poppedTouchEvent != NULL) {
        fifo_list_push(c->touchEventPool, il->poppedTouchEvent);
        il->poppedTouchEvent = NULL;
    }

    if (il->poppedKeyEvent != NULL) {
        fifo_list_push(c->keyEventPool, il->poppedKeyEvent);
        il->poppedKeyEvent = NULL;
    }

    if (il->poppedCharEvent != NULL) {
        fifo_list_push(c->charEventPool, il->poppedCharEvent);
        il->poppedCharEvent = NULL;
    }

    if (il->poppedDirPadEvent != NULL) {
        fifo_list_push(c->dirPadEventPool, il->poppedDirPadEvent);
        il->poppedDirPadEvent = NULL;
    }

    if (il->poppedActionPadEvent != NULL) {
        fifo_list_push(c->actionPadEventPool, il->poppedActionPadEvent);
        il->poppedActionPadEvent = NULL;
    }

    if (il->poppedAnalogPadEvent != NULL) {
        fifo_list_push(c->analogPadEventPool, il->poppedAnalogPadEvent);
        il->poppedAnalogPadEvent = NULL;
    }

    free(il);
}

const MouseEvent *input_listener_pop_mouse_event(InputListener *il) {

    if (il->poppedMouseEvent != NULL) {
        fifo_list_push(inputContext()->mouseEventPool, il->poppedMouseEvent);
    }

    il->poppedMouseEvent = input_event_to_MouseEvent(fifo_list_pop(il->mouseEvents));
    return il->poppedMouseEvent;
}

const TouchEvent *input_listener_pop_touch_event(InputListener *il) {

    if (il->poppedTouchEvent != NULL) {
        fifo_list_push(inputContext()->touchEventPool, il->poppedTouchEvent);
    }

    il->poppedTouchEvent = input_event_to_TouchEvent(fifo_list_pop(il->touchEvents));
    return il->poppedTouchEvent;
}

const KeyEvent *input_listener_pop_key_event(InputListener *il) {

    if (il->poppedKeyEvent != NULL) {
        fifo_list_push(inputContext()->keyEventPool, il->poppedKeyEvent);
    }

    il->poppedKeyEvent = input_event_to_KeyEvent(fifo_list_pop(il->keyEvents));
    return il->poppedKeyEvent;
}

const CharEvent *input_listener_pop_char_event(InputListener *il) {

    if (il->poppedCharEvent != NULL) {
        fifo_list_push(inputContext()->charEventPool, il->poppedCharEvent);
    }

    il->poppedCharEvent = input_event_to_CharEvent(fifo_list_pop(il->charEvents));
    return il->poppedCharEvent;
}

const DirPadEvent *input_listener_pop_dir_pad_event(InputListener *il) {

    if (il->poppedDirPadEvent != NULL) {
        fifo_list_push(inputContext()->dirPadEventPool, il->poppedDirPadEvent);
    }

    il->poppedDirPadEvent = input_event_to_DirPadEvent(fifo_list_pop(il->dirPadEvents));
    return il->poppedDirPadEvent;
}

const ActionPadEvent *input_listener_pop_action_pad_event(InputListener *il) {

    if (il->poppedActionPadEvent != NULL) {
        fifo_list_push(inputContext()->actionPadEventPool, il->poppedActionPadEvent);
    }

    il->poppedActionPadEvent = input_event_to_ActionPadEvent(fifo_list_pop(il->actionPadEvents));
    return il->poppedActionPadEvent;
}

const AnalogPadEvent *input_listener_pop_analog_pad_event(InputListener *il) {

    if (il->poppedAnalogPadEvent != NULL) {
        fifo_list_push(inputContext()->analogPadEventPool, il->poppedAnalogPadEvent);
    }

    il->poppedAnalogPadEvent = input_event_to_AnalogPadEvent(fifo_list_pop(il->analogPadEvents));
    return il->poppedAnalogPadEvent;
}

// ----------------------
// Utils
// ----------------------

void mouse_event_copy(const MouseEvent *src, MouseEvent *dst) {
    dst->button = src->button;
    dst->x = src->x;
    dst->y = src->y;
    dst->dx = src->dx;
    dst->dy = src->dy;
    dst->move = src->move;
    dst->down = src->down;
}

bool isTouchEventID(const uint8_t ID) {
    return ID < TOUCH_EVENT_MAXCOUNT;
}

bool isFinger1EventID(const uint8_t ID) {
    return ID == TOUCH_EVENT_FINGER_1;
}

bool isFinger2EventID(const uint8_t ID) {
    return ID == TOUCH_EVENT_FINGER_2;
}

bool isMouseLeftButtonID(const uint8_t ID) {
    return ID == INDEX_MOUSE_LEFTBUTTON;
}

bool isMouseRightButtonID(const uint8_t ID) {
    return ID == INDEX_MOUSE_RIGHTBUTTON;
}

void touch_event_copy(const TouchEvent *src, TouchEvent *dst) {
    dst->state = src->state;
    dst->x = src->x;
    dst->y = src->y;
    dst->dx = src->dx;
    dst->dy = src->dy;
    dst->ID = src->ID;
    dst->move = src->move;
}

void key_event_copy(const KeyEvent *src, KeyEvent *dst) {
    dst->state = src->state;
    dst->input = src->input;
    dst->modifiers = src->modifiers;
}

void char_event_copy(const CharEvent *src, CharEvent *dst) {
    dst->inputChar = src->inputChar;
}

void dir_pad_event_copy(const DirPadEvent *src, DirPadEvent *dst) {
    dst->state = src->state;
    dst->dx = src->dx;
    dst->dy = src->dy;
}

void action_pad_event_copy(const ActionPadEvent *src, ActionPadEvent *dst) {
    dst->state = src->state;
    dst->button = src->button;
}

void analog_pad_event_copy(const AnalogPadEvent *src, AnalogPadEvent *dst) {
    dst->state = src->state;
    dst->dx = src->dx;
    dst->dy = src->dy;
}
