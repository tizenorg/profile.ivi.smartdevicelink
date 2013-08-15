/*
 * Copyright (c) 2013, Ford Motor Company All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: ·
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. · Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. · Neither the name of the Ford Motor Company nor the
 * names of its contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @name SDL.SDLMediaModel
 * @desc SDL data model
 * @category Model
 * @filesource app/model/media/SDLMediaModel.js
 * @version 1.0
 */

SDL.SDLMediaModel = SDL.SDLAppModel.extend( {

    init: function() {

        this._super();

        // init properties here
        this.set( 'appInfo', Em.Object.create( {
            field1: '<field1>',
            field2: '<field2>',
            field3: '<field3>',
            mediaClock: '<mediaClock>',
            trackIcon: 'images/sdl/audio_icon.jpg',
            customPresets:
                [
                    '<no definition>',
                    '<no definition>',
                    '<no definition>',
                    '<no definition>',
                    '<no definition>',
                    '<no definition>'
                ]
        } ) );

        this.set( 'isPlaying', true );

        this.set( 'commandsList', [] );
        this.set( 'softButtons', [] );
    },

    /**
     * Flag for media playing state
     * 
     * @param {Boolean}
     */
    isPlaying: false,

    /**
     * Flag for model active state currently used for status bar
     * 
     * @param {Boolean}
     */
    active: false,

    /**
     * Timer for Media Clock
     */
    timer: null,

    /**
     * Current sdl Sub Menu identificator
     */
    currentSDLSubMenuid: null,

    /**
     * Current sdl Perform Interaction Choise identificator
     */
    currentSDLPerformInteractionChoiseId: null,

    countUp: true,
    pause: false,
    maxTimeValue: 68400, // 19 hours
    duration: 0,
    currTime: 0,

    /**
     * Method hides sdl activation button and sdl application
     * 
     * @param {Number}
     */
    onDeleteApplication: function( appId ) {
        SDL.SDLMediaController.onDeleteApplication( appId );
    },

    /**
     * Activate current application model
     */
    turnOnSDL: function() {
        SDL.SDLMediaController.activateApp( this );
    },

    startTimer: function() {

        var self = this;

        if( !this.pause ){
            this.timer = setInterval( function() {
                self.set( 'currTime', self.currTime + 1 );
            }, 1000 );
        }else{
            clearInterval( this.timer );
        }
    }.observes( 'this.pause' ),

    stopTimer: function() {
        clearInterval( this.timer );
        this.appInfo.set( 'mediaClock', '' );
    },

    setDuration: function() {

        var number, str = '', hrs = 0, min = 0, sec = 0;
        if( this.countUp ){
            number = this.duration + this.currTime;
        }else{
            number = this.duration - this.currTime;
        }

        hrs = parseInt( number / 3600 ), // hours
        min = parseInt( number / 60 ) % 60, // minutes
        sec = number % 60; // seconds

        str = ( hrs < 10 ? '0' : '' ) + hrs + ':';
        str += ( min < 10 ? '0' : '' ) + min + ":";
        str += ( sec < 10 ? '0' : '' ) + sec;
        this.appInfo.set( 'mediaClock', str );

        if( !this.get( 'countUp' ) && this.duration == this.currTime ){
            clearInterval( this.timer );
            return;
        }

    }.observes( 'this.currTime' ),

    changeDuration: function() {
        clearInterval( this.timer );
        this.currTime = 0;
        this.startTimer();
    }.observes( 'this.duration' ),

    /**
     * SDL Setter for Media Clock Timer
     * 
     * @param {Object}
     */
    sdlSetMediaClockTimer: function( params ) {

        if( ( params.updateMode == "PAUSE" && this.pause ) || ( params.updateMode == "RESUME" && !this.pause ) ){
            return 'IGNORED';
        }

        if( params.updateMode == "CLEAR" ){
            this.stopTimer();
            return 'SUCCESS';
        }

        if( params.updateMode == "PAUSE" ){
            this.set( 'pause', true );
        }else if( params.updateMode == "RESUME" ){
            this.set( 'pause', false );
        }else{
            if( params.startTime ){
                this.set( 'countUp', params.updateMode == "COUNTUP" ? true : false );
                this.set( 'duration', 0 );
                this.set( 'duration', params.startTime.hours * 3600 + params.startTime.minutes * 60 + params.startTime.seconds );
            }
            this.set( 'pause', false );
        }

        return 'SUCCESS';
    },

    /**
     * Method to clear App OverLay
     */
    clearAppOverLay: function() {

        clearInterval( this.timer );
        this.appInfo.set( 'field1', '' );
        this.appInfo.set( 'field2', '' );
        this.appInfo.set( 'field3', '' );
        this.appInfo.set( 'field4', '' );
        this.appInfo.set( 'alignment', '' );
        this.set( 'statusText', '' );
        this.appInfo.set( 'mediaClock', '' );
        this.appInfo.set( 'mediaTrack', '' );
        this.appInfo.set( 'trackIcon', 'images/sdl/audio_icon.jpg' );
        this.updateSoftButtons();
        for( i = 0; i < 6; i++ ){
            this.appInfo.set( 'customPresets.' + i, '' );
        }
        SDL.SDLModel.set( 'protocolVersion2State', false );

    },

    /**
     * Applin UI Show handler
     * 
     * @param {Object}
     */
    onSDLUIShow: function( params ) {
        clearInterval( this.timer );
        this.appInfo.set( 'field1', params.mainField1 );
        this.appInfo.set( 'field2', params.mainField2 );
        this.appInfo.set( 'field3', params.mainField3 );
        this.appInfo.set( 'field4', params.mainField4 );
        this.appInfo.set( 'alignment', params.alignment );
        this.set( 'statusText', params.statusBar );
        this.appInfo.set( 'mediaClock', params.mediaClock );
        this.appInfo.set( 'mediaTrack', params.mediaTrack );
        if( params.graphic ){
            this.appInfo.set( 'trackIcon', params.graphic );
        }else{
            this.appInfo.set( 'trackIcon', 'images/sdl/audio_icon.jpg' );
        }

        this.updateSoftButtons( params.softButtons );

        if( params.customPresets ){
            var i = 0;
            for( i = 0; i < 6; i++ ){
                if( params.customPresets[i] != '' || params.customPresets[i] != null ){
                    this.appInfo.set( 'customPresets.' + i, params.customPresets[i] );
                }else{
                    this.appInfo.set( 'customPresets.' + i, '' );
                }
            }
            SDL.SDLModel.set( 'protocolVersion2State', true );
        }else{
            SDL.SDLModel.set( 'protocolVersion2State', false );
        }
    }
} );