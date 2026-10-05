#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"

#include "game_controller.hpp"


@interface AegisViewController : NSViewController <MTKViewDelegate>
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@end


@implementation AegisViewController

- (instancetype)init
{
    self = [super init];

    if (self)
    {
        _device = MTLCreateSystemDefaultDevice();
        _commandQueue = [_device newCommandQueue];

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();

        ImGui_ImplMetal_Init(_device);
    }

    return self;
}


- (void)loadView
{
    MTKView* view =
        [[MTKView alloc]
            initWithFrame:NSMakeRect(
                0,
                0,
                600,
                400
            )
        ];

    view.device = self.device;
    view.delegate = self;

    self.view = view;
}


- (void)viewDidLoad
{
    [super viewDidLoad];

    ImGui_ImplOSX_Init(self.view);
}


- (void)drawInMTKView:(MTKView*)view
{
    MTLRenderPassDescriptor* pass =
        view.currentRenderPassDescriptor;

    if (pass == nil)
        return;

    id<MTLCommandBuffer> commandBuffer =
        [self.commandQueue commandBuffer];


    ImGui_ImplMetal_NewFrame(pass);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();


    static float walkSpeed = 19.0f;

    static GameController controller;

    static std::string resultMessage =
        "No Walk Speed change requested yet.";


    ImGui::Begin("AegisLab");

    ImGui::Text("Audaciga Developer Tools");

    ImGui::Separator();


    ImGui::InputFloat(
        "Walk Speed",
        &walkSpeed,
        1.0f,
        5.0f
    );


    if (ImGui::Button("Apply Walk Speed"))
    {
        ApplyResult result =
            controller.applyWalkSpeed(walkSpeed);

        resultMessage =
            result.message;
    }


    ImGui::TextWrapped(
        "%s",
        resultMessage.c_str()
    );


    ImGui::End();


    ImGui::Render();


    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(
            0.08,
            0.08,
            0.08,
            1.0
        );


    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer
            renderCommandEncoderWithDescriptor:pass
        ];


    ImGui_ImplMetal_RenderDrawData(
        ImGui::GetDrawData(),
        commandBuffer,
        encoder
    );


    [encoder endEncoding];


    [commandBuffer
        presentDrawable:view.currentDrawable
    ];


    [commandBuffer commit];
}


- (void)mtkView:(MTKView*)view
    drawableSizeWillChange:(CGSize)size
{
}

@end


@interface AegisAppDelegate :
    NSObject <NSApplicationDelegate>

@property(nonatomic, strong) NSWindow* window;

@end


@implementation AegisAppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication*)sender
{
    return YES;
}


- (void)applicationDidFinishLaunching:
    (NSNotification*)notification
{
    AegisViewController* controller =
        [[AegisViewController alloc] init];


    self.window =
        [[NSWindow alloc]
            initWithContentRect:
                NSMakeRect(
                    0,
                    0,
                    600,
                    400
                )

            styleMask:
                NSWindowStyleMaskTitled |
                NSWindowStyleMaskClosable |
                NSWindowStyleMaskResizable |
                NSWindowStyleMaskMiniaturizable

            backing:NSBackingStoreBuffered

            defer:NO
        ];


    self.window.title = @"AegisLab";

    self.window.contentViewController =
        controller;


    [self.window center];

    [self.window makeKeyAndOrderFront:nil];

    [NSApp activateIgnoringOtherApps:YES];
}

@end


int main()
{
    @autoreleasepool
    {
        NSApplication* app =
            [NSApplication sharedApplication];


        AegisAppDelegate* delegate =
            [[AegisAppDelegate alloc] init];


        app.delegate =
            delegate;


        [app setActivationPolicy:
            NSApplicationActivationPolicyRegular
        ];


        [app run];
    }

    return 0;
}
