jQuery(function($){
    
    $('#topNav .navbar-header [data-toggle=collapse]').click(function(e) {
        
        e.stopImmediatePropagation();
        //e.preventDefault();
         $('#global').toggleClass('active');
        //
    });

    if($.fn.slider){
        $(".slider").slider();

        $(window).resize(function(){

            $('.slider.responsive').each(function(){
                var $this = $(this);
                var parent = $this.closest('.sliderContainer');
                var parentWidth = parent.width();

                var sliderElement = $this.closest('div.slider');
                var siblings = sliderElement.siblings();
                var siblingWidth = 10;
                siblings.each(function(){
                    siblingWidth += $(this).outerWidth(true);
                });

                sliderElement.css('width', parentWidth - siblingWidth);

            });

        }).resize();
    }
});