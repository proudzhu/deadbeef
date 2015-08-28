/*
    DeaDBeeF -- the music player
    Copyright (C) 2009-2015 Alexey Yakovenko and other contributors

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.

    3. This notice may not be removed or altered from any source distribution.
*/

#import "MainWindowController.h"
#import "INAppStoreWindow.h"
#include "../../deadbeef.h"
#include <sys/time.h>

extern DB_functions_t *deadbeef;

@interface MainWindowController () {
    NSTimer *_updateTimer;
}

@end
@interface NSView (AppKitDetails)
- (void)_addKnownSubview:(NSView *)subview;
@end


@implementation MainWindowController

- (void)dealloc {
    if (_updateTimer) {
        [_updateTimer invalidate];
        _updateTimer = nil;
    }
}

- (void)initCustomTitlebar {
    INAppStoreWindow *aWindow = (INAppStoreWindow*)self.window;
    aWindow.titleBarHeight = 44.0;

    NSView *themeFrame = [[self.window contentView] superview];
    NSRect c = [themeFrame frame];

#if 0
    NSButton *btnClose = [NSWindow standardWindowButton:NSWindowCloseButton forStyleMask:NSTitledWindowMask];
    NSRect fr = [btnClose frame];
    fr.origin.x = 10;
    fr.origin.y = c.size.height - fr.size.height - 13;
    [btnClose removeFromSuperview];
    [btnClose setFrame:fr];
    [btnClose setAutoresizingMask:NSViewMaxXMargin];
    if ([themeFrame respondsToSelector:@selector(_addKnownSubview:)])
        [themeFrame _addKnownSubview:btnClose];
    else
        [themeFrame addSubview:btnClose];

    NSButton *btnMin = [self.window standardWindowButton:NSWindowMiniaturizeButton];
    fr = [btnMin frame];
    fr.origin.x = 30;
    fr.origin.y = c.size.height - fr.size.height - 13;
    [btnMin removeFromSuperview];
    [btnMin setFrame:fr];
    if ([themeFrame respondsToSelector:@selector(_addKnownSubview:)])
        [themeFrame _addKnownSubview:btnMin];
    else
        [themeFrame addSubview:btnMin];

    NSButton *btnZoom = [self.window standardWindowButton:NSWindowZoomButton];
    fr = [btnZoom frame];
    fr.origin.x = 50;
    fr.origin.y = c.size.height - fr.size.height - 13;
    [btnZoom removeFromSuperview];
    [btnZoom setFrame:fr];
    if ([themeFrame respondsToSelector:@selector(_addKnownSubview:)])
        [themeFrame _addKnownSubview:btnZoom];
    else
        [themeFrame addSubview:btnZoom];
#endif

    NSRect fr = [[self.window standardWindowButton:NSWindowZoomButton] frame];

    NSRect btnBarFrame = [_buttonBar frame];
    btnBarFrame = NSMakeRect(fr.origin.x + fr.size.width + 10, c.size.height - btnBarFrame.size.height-10, btnBarFrame.size.width, btnBarFrame.size.height);
    [_buttonBar removeFromSuperview];
    [_buttonBar setFrame:btnBarFrame];

    if ([themeFrame respondsToSelector:@selector(_addKnownSubview:)])
        [themeFrame _addKnownSubview:_buttonBar];
    else
        [themeFrame addSubview:_buttonBar];

    NSRect seekBarFrame = [_seekBar frame];
    int x = btnBarFrame.origin.x + btnBarFrame.size.width + 10;
    seekBarFrame = NSMakeRect(x, c.size.height - seekBarFrame.size.height-12, c.size.width - x - 10, seekBarFrame.size.height);
    [_seekBar removeFromSuperview];
    [_seekBar setFrame:seekBarFrame];

    if ([themeFrame respondsToSelector:@selector(_addKnownSubview:)])
        [themeFrame _addKnownSubview:_seekBar];
    else
        [themeFrame addSubview:_seekBar];
}

- (void)windowDidLoad {
    [super windowDidLoad];

    [self initCustomTitlebar];

    _updateTimer = [NSTimer timerWithTimeInterval:1.0f/10.0f target:self selector:@selector(frameUpdate:) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_updateTimer forMode:NSDefaultRunLoopMode];    
}

int prevSeekbar = -1;

// update status bar and window title
static char sb_text[512];
static char sbitrate[20] = "";
static struct timeval last_br_update;

#define _(x) x

- (void)updateSonginfo {
    DB_output_t *output = deadbeef->get_output ();
    char sbtext_new[512] = "-";
    
    float pl_totaltime = deadbeef->pl_get_totaltime ();
    int daystotal = (int)pl_totaltime / (3600*24);
    int hourtotal = ((int)pl_totaltime / 3600) % 24;
    int mintotal = ((int)pl_totaltime/60) % 60;
    int sectotal = ((int)pl_totaltime) % 60;
    
    char totaltime_str[512] = "";
    if (daystotal == 0) {
        snprintf (totaltime_str, sizeof (totaltime_str), "%d:%02d:%02d", hourtotal, mintotal, sectotal);
    }
    else if (daystotal == 1) {
        snprintf (totaltime_str, sizeof (totaltime_str), _("1 day %d:%02d:%02d"), hourtotal, mintotal, sectotal);
    }
    else {
        snprintf (totaltime_str, sizeof (totaltime_str), _("%d days %d:%02d:%02d"), daystotal, hourtotal, mintotal, sectotal);
    }
    
    DB_playItem_t *track = deadbeef->streamer_get_playing_track ();
    DB_fileinfo_t *c = deadbeef->streamer_get_current_fileinfo (); // FIXME: might crash streamer
    
    float duration = track ? deadbeef->pl_get_item_duration (track) : -1;
    
    if (!output || (output->state () == OUTPUT_STATE_STOPPED || !track || !c)) {
        snprintf (sbtext_new, sizeof (sbtext_new), _("Stopped | %d tracks | %s total playtime"), deadbeef->pl_getcount (PL_MAIN), totaltime_str);
    }
    else {
        float playpos = deadbeef->streamer_get_playpos ();
        int minpos = playpos / 60;
        int secpos = playpos - minpos * 60;
        int mindur = duration / 60;
        int secdur = duration - mindur * 60;
        
        const char *mode;
        char temp[20];
        if (c->fmt.channels <= 2) {
            mode = c->fmt.channels == 1 ? _("Mono") : _("Stereo");
        }
        else {
            snprintf (temp, sizeof (temp), "%dch Multichannel", c->fmt.channels);
            mode = temp;
        }
        int samplerate = c->fmt.samplerate;
        int bitspersample = c->fmt.bps;
        //        codec_unlock ();
        
        char t[100];
        if (duration >= 0) {
            snprintf (t, sizeof (t), "%d:%02d", mindur, secdur);
        }
        else {
            strcpy (t, "-:--");
        }
        
        struct timeval tm;
        gettimeofday (&tm, NULL);
        if (tm.tv_sec - last_br_update.tv_sec + (tm.tv_usec - last_br_update.tv_usec) / 1000000.0 >= 0.3) {
            memcpy (&last_br_update, &tm, sizeof (tm));
            int bitrate = deadbeef->streamer_get_apx_bitrate ();
            if (bitrate > 0) {
                snprintf (sbitrate, sizeof (sbitrate), _("| %4d kbps "), bitrate);
            }
            else {
                sbitrate[0] = 0;
            }
        }
        const char *spaused = deadbeef->get_output ()->state () == OUTPUT_STATE_PAUSED ? _("Paused | ") : "";
        char filetype[20];
        if (!deadbeef->pl_get_meta (track, ":FILETYPE", filetype, sizeof (filetype))) {
            strcpy (filetype, "-");
        }
        snprintf (sbtext_new, sizeof (sbtext_new), _("%s%s %s| %dHz | %d bit | %s | %d:%02d / %s | %d tracks | %s total playtime"), spaused, filetype, sbitrate, samplerate, bitspersample, mode, minpos, secpos, t, deadbeef->pl_getcount (PL_MAIN), totaltime_str);
    }
    
    if (strcmp (sbtext_new, sb_text)) {
        strcpy (sb_text, sbtext_new);
        [[self statusBar] setStringValue:[NSString stringWithUTF8String:sb_text]];
    }
    
    if (track) {
        deadbeef->pl_item_unref (track);
    }
}

- (void)frameUpdate:(id)userData
{
    if (![[self window] isVisible]) {
        return;
    }
    float dur = -1;
    float perc = 0;
    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        dur = deadbeef->pl_get_item_duration (trk);
        if (dur >= 0) {
            perc = deadbeef->streamer_get_playpos () / dur * 100.f;
            if (perc < 0) {
                perc = 0;
            }
            else if (perc > 100) {
                perc = 100;
            }
        }
        deadbeef->pl_item_unref (trk);
    }
    
    int cmp =(int)(perc*4000);
    if (cmp != prevSeekbar) {
        prevSeekbar = cmp;
        [_seekBar setFloatValue:perc];
    }
    
    BOOL st = YES;
    if (!trk || dur < 0) {
        st = NO;
    }
    if ([_seekBar isEnabled] != st) {
        [_seekBar setEnabled:st];
    }
    
    [self updateSonginfo];    
}

- (IBAction)seekBarAction:(id)sender {
    DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
    if (trk) {
        float dur = deadbeef->pl_get_item_duration (trk);
        if (dur >= 0) {
            float time = [(NSSlider*)sender floatValue] / 100.f;
            time *= dur;
            deadbeef->sendmessage (DB_EV_SEEK, 0, time * 1000, 0);
        }
        deadbeef->pl_item_unref (trk);
    }
}

- (IBAction)tbClicked:(id)sender {
    NSInteger selectedSegment = [sender selectedSegment];
    
    switch (selectedSegment) {
        case 0:
            deadbeef->sendmessage(DB_EV_PREV, 0, 0, 0);
            break;
        case 1:
            deadbeef->sendmessage(DB_EV_PLAY_CURRENT, 0, 0, 0);
            break;
        case 2:
            deadbeef->sendmessage(DB_EV_TOGGLE_PAUSE, 0, 0, 0);
            break;
        case 3:
            deadbeef->sendmessage(DB_EV_STOP, 0, 0, 0);
            break;
        case 4:
            deadbeef->sendmessage(DB_EV_NEXT, 0, 0, 0);
            break;
    }
}

- (IBAction)performCloseTabAction:(id)sender {
    int idx = deadbeef->plt_get_curr_idx ();
    if (idx != -1) {
        deadbeef->plt_remove (idx);
    }
}

- (IBAction)renamePlaylistAction:(id)sender {
    ddb_playlist_t *plt = deadbeef->plt_get_for_idx ([_tabStrip clickedTab]);
    int l = deadbeef->plt_get_title (plt, NULL, 0);
    char buf[l+1];
    deadbeef->plt_get_title (plt, buf, (int)sizeof buf);
    deadbeef->plt_unref (plt);
    [_renamePlaylistTitle setStringValue:[NSString stringWithUTF8String:buf]];
    [NSApp beginSheet:self.renamePlaylistWindow modalForWindow:[self window] modalDelegate:self didEndSelector:@selector(didEndRenamePlaylist:returnCode:contextInfo:) contextInfo:nil];
}

- (void)didEndRenamePlaylist:(NSWindow *)sheet returnCode:(NSInteger)returnCode contextInfo:(void *)contextInfo
{
    [sheet orderOut:self];

    if (returnCode == NSOKButton) {
        ddb_playlist_t *plt = deadbeef->plt_get_for_idx ([_tabStrip clickedTab]);
        deadbeef->plt_set_title (plt, [[_renamePlaylistTitle stringValue] UTF8String]);
        deadbeef->plt_save_config (plt);
        deadbeef->plt_unref (plt);
    }
}

- (IBAction)renamePlaylistCancelAction:(id)sender {
    [NSApp endSheet:self.renamePlaylistWindow returnCode:NSCancelButton];
}

- (IBAction)renamePlaylistOKAction:(id)sender {
    [NSApp endSheet:self.renamePlaylistWindow returnCode:NSOKButton];
}

@end
