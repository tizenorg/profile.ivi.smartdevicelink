/*
 * Copyright (c) 2013, Ford Motor Company All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  · Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *  · Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *  · Neither the name of the Ford Motor Company nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
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
 * @name SDL.SliderView
 * @desc Slider visual representation
 * @category View
 * @filesource app/view/sdl/shared/sliderView.js
 * @version 1.0
 */

SDL.SliderView = SDL.SDLAbstractView.create( {

    elementId: 'slider_view',

    childViews:
        [
            'backButton',
            'captionText',
            'headerLabel',
            'footerLabel',
            'adjustControl'
        ],

    headerLabel: SDL.Label.extend( {
        classNames: 'slider-header',
        content: 'Header Label'
    } ),

    footerLabel: SDL.Label.extend( {
        classNames: 'slider-footer',
        content: 'Footer Label',
        data: []
    } ),

    /**
     * Identifier of current request
     */
    sliderRequestId: null,

    /**
     * Extend deactivate method send SUCCESS response on deactivate with current
     * slider value
     */
    deactivate: function() {
        this._super();
        FFW.UI.sendSliderResult( "SUCCESS", this.get( 'sliderRequestId' ), this.get( 'adjustControl.sliderValue.value' ) );
    },

    adjustControl: Em.ContainerView.extend( {

        classNames: 'sliderControl',

        childViews:
            [
                'minusBtn',
                'led',
                'plusBtn'
            ],

        sliderValue: Em.Object.create( {
            range: 10,
            value: 0
        } ),

        minusBtn: SDL.Button.extend( {
            classNames: 'control minus',
            icon: 'images/common/minus-ico.png',
            actionDown: function() {
                this._super();
                this.set( 'parentView.sliderValue.value', this._parentView.sliderValue.value - 1 );
            }
        } ),

        led: SDL.Indicator.create( {
            classNames: 'ledContainer ico',
            contentBinding: 'parentView.sliderValue',
            indActiveClass: 'led',
            indDefaultClass: 'led-inactive'
        } ),

        plusBtn: SDL.Button.extend( {
            classNames: 'control plus',
            icon: 'images/common/plus-ico.png',
            actionDown: function() {
                this._super();
                this.set( 'parentView.sliderValue.value', this._parentView.sliderValue.value + 1 );
            }
        } )
    } ),

    loadData: function( message ) {

        var data = message.params;

        this.set( 'sliderRequestId', message.id );

        this.set( 'headerLabel.content', data.sliderHeader );
        this.set( 'footerLabel.content', data.sliderFooter[0] );
        this.get( 'adjustControl.sliderValue' ).set( 'range', data.numTicks );
        this.get( 'adjustControl.sliderValue' ).set( 'value', data.position );

        this.footerLabel.data = data.sliderFooter;

        setTimeout( function() {
            SDL.SliderView.adjustControl.rerender();
        }, 1 );
    },

    /**
     * Change footer text depends on current slider position works only for
     * dynamic footer mode
     */
    changeFooterText: function() {
        if( this.footerLabel.data.length > 1 ){
            this.set( 'footerLabel.content', this.footerLabel.data[this.adjustControl.sliderValue.value - 1] );
        }
    }.observes( 'adjustControl.sliderValue.value' )
} );
