//
//  AUInstrumentAudioUnit.h
//  AUInstrument
//
//  Created by Hallgrim Bratberg on 11/08/2021.
//

#import <AudioToolbox/AudioToolbox.h>
#import "AUInstrumentDSPKernelAdapter.h"

// Define parameter addresses.
extern const AudioUnitParameterID myParam1;

@interface AUInstrumentAudioUnit : AUAudioUnit

@property (nonatomic, readonly) AUInstrumentDSPKernelAdapter *kernelAdapter;
- (void)setupAudioBuses;
- (void)setupParameterTree;
- (void)setupParameterCallbacks;
@end
