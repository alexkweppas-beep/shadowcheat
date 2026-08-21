import React, { useState, useRef } from 'react';
import './ImageComparison.css';

export default function ImageComparison({ beforeImage, afterImage }) {
  const [sliderPosition, setSliderPosition] = useState(50);
  const [isDragging, setIsDragging] = useState(false);
  const containerRef = useRef(null);

  const handleMove = (clientX) => {
    if (!containerRef.current) return;
    const rect = containerRef.current.getBoundingClientRect();
    const x = Math.max(0, Math.min(clientX - rect.left, rect.width));
    const percent = Math.max(0, Math.min((x / rect.width) * 100, 100));
    setSliderPosition(percent);
  };

  const onMouseMove = (e) => {
    if (!isDragging) return;
    handleMove(e.clientX);
  };

  const onTouchMove = (e) => {
    if (!isDragging) return;
    handleMove(e.touches[0].clientX);
  };

  const onMouseDown = (e) => {
    setIsDragging(true);
    handleMove(e.clientX);
  };

  const onTouchStart = (e) => {
    setIsDragging(true);
    handleMove(e.touches[0].clientX);
  };

  const stopDragging = () => {
    setIsDragging(false);
  };

  return (
    <div 
      className="image-comparison-container"
      ref={containerRef}
      onMouseMove={onMouseMove}
      onMouseUp={stopDragging}
      onMouseLeave={stopDragging}
      onTouchMove={onTouchMove}
      onTouchEnd={stopDragging}
      onTouchCancel={stopDragging}
      onMouseDown={onMouseDown}
      onTouchStart={onTouchStart}
    >
      <div className="image-comparison-images">
        {/* Right side: With ESP */}
        <img src={afterImage} alt="With ESP" className="image-after" draggable="false" />
        
        {/* Left side: Without ESP (Clipped) */}
        <div 
          className="image-before-wrapper" 
          style={{ clipPath: `inset(0 ${100 - sliderPosition}% 0 0)` }}
        >
          <img src={beforeImage} alt="Without ESP" className="image-before" draggable="false" />
        </div>
      </div>

      <div 
        className="comparison-slider-line" 
        style={{ left: `${sliderPosition}%` }}
      >
        <div className="comparison-slider-button">
          <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M10 18L4 12L10 6" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/>
            <path d="M14 6L20 12L14 18" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"/>
          </svg>
        </div>
      </div>
    </div>
  );
}
