Polymer({

    is: 'paper-card',

    properties: {

      /**
       * The title of the card.
       */
      heading: {
        type: String,
        value: '',
        observer: '_headingChanged'
      },

      /**
       * The url of the title image of the card.
       */
      image: {
        type: String,
        value: ''
      },

      /**
       * The z-depth of the card, from 0-5.
       */
      elevation: {
        type: Number,
        value: 1
      },

      /**
       * Set this to true to animate the card shadow when setting a new
       * `z` value.
       */
      animatedShadow: {
        type: Boolean,
        value: false
      }
    },

    _headingChanged: function(heading) {
      var label = this.getAttribute('aria-label');
      this.setAttribute('aria-label', heading);
    },

    _computeHeadingClass: function(image) {
      var cls = 'title-text';
      if (image)
        cls += ' over-image';
      return cls;
    }
  });